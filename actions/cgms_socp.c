#include <errno.h>
#include <string.h>
#include <logging/log.h>

#include "atb_ble_cgms.h"
#include "cgms_socp.h"
#include "cgms_db.h"
#include "cgms_meas.h"
#include "cgms_sst.h"

LOG_MODULE_REGISTER(cgms_socp, LOG_LEVEL_DBG);

void cgms_socp_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	m_socp_ind_enabled = (value == BT_GATT_CCC_INDICATE);
}

static bool cgms_socp_response_has_result_code(uint8_t opcode)
{
	switch (opcode) {
	case SOCP_READ_CGM_COMM_INTERVAL_RSP:
	case SOCP_READ_GLUCOSE_CALIBRATION_VALUE_RESPONSE:
	case SOCP_READ_PATIENT_HIGH_ALERT_LEVEL_RESPONSE:
	case SOCP_READ_PATIENT_LOW_ALERT_LEVEL_RESPONSE:
	case SOCP_HYPO_ALERT_LEVEL_RESPONSE:
	case SOCP_HYPER_ALERT_LEVEL_RESPONSE:
	case SOCP_RATE_OF_DECREASE_ALERT_LEVEL_RESPONSE:
	case SOCP_RATE_OF_INCREASE_ALERT_LEVEL_RESPONSE:
		return false;
	default:
		return true;
	}
}

static uint8_t cgms_socp_decode_u16(const ble_cgms_socp_value_t *req, uint16_t *value)
{
	if ((req == NULL) || (value == NULL) || (req->operand_len != sizeof(uint16_t)) || (req->p_operand == NULL)) {
		return SOCP_RSP_INVALID_OPERAND;
	}

	*value = get_le16(req->p_operand);
	if ((*value == NRF_BLE_CGMS_PLUS_INFINITE) || (*value == NRF_BLE_CGMS_MINUS_INFINITE)) {
		return SOCP_RSP_OUT_OF_RANGE;
	}

	return SOCP_RSP_SUCCESS;
}

static void cgms_socp_ind_cb(struct bt_conn *conn, struct bt_gatt_indicate_params *params, uint8_t err)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);
	ARG_UNUSED(err);
}

static int cgms_socp_indicate(const uint8_t *data, uint16_t len)
{
	if ((m_conn == NULL) || !m_socp_ind_enabled) {
		return -ENOTCONN;
	}

	memcpy(m_socp_ind_buf, data, len);
	memset(&m_socp_ind_params, 0, sizeof(m_socp_ind_params));
	m_socp_ind_params.attr = &attr_cgms_svc[CGMS_ATTR_SOCP_VAL];
	m_socp_ind_params.data = m_socp_ind_buf;
	m_socp_ind_params.len = len;
	m_socp_ind_params.func = cgms_socp_ind_cb;
	return bt_gatt_indicate(m_conn, &m_socp_ind_params);
}

static void ble_socp_decode(uint8_t data_len, uint8_t const * p_data, ble_cgms_socp_value_t * p_socp_val)
{
    p_socp_val->opcode      = 0xFF;
    p_socp_val->operand_len = 0;
    p_socp_val->p_operand   = NULL;

    if (data_len > 0)
    {
        p_socp_val->opcode = p_data[0];
    }
    if (data_len > 1)
    {
        p_socp_val->operand_len = data_len - 1;
        p_socp_val->p_operand   = (uint8_t*)&p_data[1]; // lint !e416
    }
}

static uint8_t ble_socp_encode(const ble_socp_rsp_t * p_socp_rsp, uint8_t * p_data)
{
    uint8_t len = 0;
    int     i;


    if (p_data != NULL)
    {
        p_data[len++] = p_socp_rsp->opcode;

        if (
			(p_socp_rsp->opcode != SOCP_READ_CGM_COMM_INTERVAL_RSP)
            && (p_socp_rsp->opcode != SOCP_READ_PATIENT_HIGH_ALERT_LEVEL_RESPONSE)
            && (p_socp_rsp->opcode != SOCP_READ_PATIENT_LOW_ALERT_LEVEL_RESPONSE)
            && (p_socp_rsp->opcode != SOCP_HYPO_ALERT_LEVEL_RESPONSE)
            && (p_socp_rsp->opcode != SOCP_HYPER_ALERT_LEVEL_RESPONSE)
            && (p_socp_rsp->opcode != SOCP_RATE_OF_DECREASE_ALERT_LEVEL_RESPONSE)
            && (p_socp_rsp->opcode != SOCP_RATE_OF_INCREASE_ALERT_LEVEL_RESPONSE)
            && (p_socp_rsp->opcode != SOCP_READ_GLUCOSE_CALIBRATION_VALUE_RESPONSE)
           )
        {
            p_data[len++] = p_socp_rsp->req_opcode;
            p_data[len++] = p_socp_rsp->rsp_code;
        }

        for (i = 0; i < p_socp_rsp->size_val; i++)
        {
            p_data[len++] = p_socp_rsp->resp_val[i];
        }
    }

    return len;
}

static int cgms_socp_send_response(ble_socp_rsp_t rsp)
{
	uint8_t buf[20];
	uint8_t len;

	if (rsp.size_val > sizeof(rsp.resp_val)) {
		return -EINVAL;
	}

	// Send indication
    len = ble_socp_encode(&rsp, m_socp_ind_buf);
	if (len == 0U) {
		return -EINVAL;
	}

	return cgms_socp_indicate(m_socp_ind_buf, len);
}

static bool is_feature_present(uint32_t feature)
{
    return (m_feature.feature & feature);
}

ssize_t cgms_write_socp(struct bt_conn *conn, const struct bt_gatt_attr *attr,
	const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	LOG_DBG("Write SOCP: conn %p, attr %p, buf %p, len %u, offset %u, flags %u",
		conn, attr, buf, len, offset, flags);
	// 请求码
	ble_cgms_socp_value_t                 socp_request;
	// 回复码
	ble_socp_rsp_t                        rsp;
	uint32_t                              err_code;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (!m_socp_ind_enabled) {
		return BT_GATT_ERR(BT_ATT_ERR_CCC_IMPROPER_CONF);
	}

	// 解码opcode和参数
	memset(&socp_request, 0, sizeof(socp_request));
	memset(&rsp, 0, sizeof(rsp));
	ble_socp_decode((uint8_t)len, (const uint8_t *)buf, &socp_request);

	rsp.opcode     = SOCP_RESPONSE_CODE;
	rsp.req_opcode = socp_request.opcode;
	rsp.rsp_code   = SOCP_RSP_OP_CODE_NOT_SUPPORTED;
	rsp.size_val   = 0;

	switch (socp_request.opcode) {
		// 写通信间隔
		case SOCP_WRITE_CGM_COMMUNICATION_INTERVAL:
			// 新增间隔设置检查 
			if (socp_request.operand_len < 1U) {
				rsp.rsp_code = SOCP_RSP_INVALID_OPERAND;
				break;
			}
			rsp.rsp_code = SOCP_RSP_SUCCESS;
			m_comm_interval = socp_request.p_operand[0];
			// 处理通信间隔参数事件
			cgms_emit_event(BLE_CGMS_EVT_WRITE_COMM_INTERVAL);
			break;

		case SOCP_READ_CGM_COMMUNICATION_INTERVAL:
			rsp.rsp_code      = SOCP_READ_CGM_COMM_INTERVAL_RSP;
			rsp.resp_val[0] = m_comm_interval;
			rsp.size_val++;
			break;
		
		// 开始会话
		case SOCP_START_THE_SESSION:
			if (m_session_started) {
				rsp.rsp_code = SOCP_RSP_PROCEDURE_NOT_COMPLETED;
			}
			// 更新feature
			else if ((m_nb_run_session != 0U) && 
					!is_feature_present(NRF_BLE_CGMS_FEAT_MULTIPLE_SESSIONS_SUPPORTED))
			{
				rsp.rsp_code = SOCP_RSP_PROCEDURE_NOT_COMPLETED;
			}
			else
			{
				rsp.rsp_code = SOCP_RSP_SUCCESS;
				m_session_started     = true;
				m_nb_run_session++;
				// 发出开始会话事件
				cgms_emit_event(BLE_CGMS_EVT_START_SESSION);

				ble_cgms_sst_t sst;
				memset(&sst, 0, sizeof(ble_cgms_sst_t));

				err_code = cgms_sst_set(&sst);
				if (err_code != NRF_SUCCESS) {
					rsp.rsp_code = SOCP_RSP_PROCEDURE_NOT_COMPLETED;
					LOG_ERR("Failed to set SST (err %d)\n", err_code);
					return len;
				}
				m_status.time_offset    = 0;
				m_status.status.status &= (~NRF_BLE_CGMS_STATUS_SESSION_STOPPED);
				
				err_code = nrf_ble_cgms_update_status(&m_status);
				if (err_code != NRF_SUCCESS)
				{
					rsp.rsp_code = SOCP_RSP_PROCEDURE_NOT_COMPLETED;
					LOG_ERR("Failed to update status (err %d)\n", err_code);
					return len;
				}
			}
			break;
		// 停止会话
		case SOCP_STOP_THE_SESSION:
			{
				nrf_ble_cgm_status_t status;
				memset(&status, 0, sizeof(nrf_ble_cgm_status_t));
				
				rsp.rsp_code = SOCP_RSP_SUCCESS;
				m_session_started     = false;

				status.time_offset   = m_status.time_offset;
				status.status.status = m_status.status.status |
									NRF_BLE_CGMS_STATUS_SESSION_STOPPED;
				// 发出停止会话事件
				cgms_emit_event(BLE_CGMS_EVT_STOP_SESSION);

				err_code = nrf_ble_cgms_update_status(&status);
				if (err_code != 0)
				{
					rsp.rsp_code = SOCP_RSP_PROCEDURE_NOT_COMPLETED;
					LOG_ERR("Failed to update status (err %d)\n", err_code);
					return len;
				}
				break;
			}
		default:
			rsp.rsp_code = SOCP_RSP_OP_CODE_NOT_SUPPORTED;
			break;
		}
	(void)cgms_socp_send_response(rsp);
	return len;
}
