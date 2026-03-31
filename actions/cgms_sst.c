#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/timeutil.h>

#include "cgms_sst.h"
#include "cgms_db.h"

static uint8_t ble_date_time_decode_local(ble_date_time_t *p_date_time, const uint8_t *p_data)
{
    uint8_t index = 0U;

    p_date_time->year = get_le16(&p_data[index]);
    index += 2U;
    p_date_time->month = p_data[index++];
    p_date_time->day = p_data[index++];
    p_date_time->hours = p_data[index++];
    p_date_time->minutes = p_data[index++];
    p_date_time->seconds = p_data[index++];

    return index;
}

static uint8_t ble_date_time_encode_local(const ble_date_time_t *p_date_time, uint8_t *p_data)
{
    uint8_t index = 0U;

    put_le16(&p_data[index], p_date_time->year);
    index += 2U;
    p_data[index++] = p_date_time->month;
    p_data[index++] = p_date_time->day;
    p_data[index++] = p_date_time->hours;
    p_data[index++] = p_date_time->minutes;
    p_data[index++] = p_date_time->seconds;

    return index;
}

void sst_decode(ble_cgms_sst_t *p_sst, const uint8_t *p_data, uint16_t len)
{
    uint8_t index;

    if ((p_sst == NULL) || (p_data == NULL) || (len != NRF_BLE_CGMS_SST_LEN)) {
        return;
    }

    index = ble_date_time_decode_local(&p_sst->date_time, p_data);
    p_sst->time_zone = p_data[index++];
    p_sst->dst = p_data[index++];
}

static void convert_ble_time_c_time(const ble_cgms_sst_t *p_sst, struct tm *p_c_time_date)
{
    p_c_time_date->tm_sec = p_sst->date_time.seconds;
    p_c_time_date->tm_min = p_sst->date_time.minutes;
    p_c_time_date->tm_hour = p_sst->date_time.hours;
    p_c_time_date->tm_mday = p_sst->date_time.day;
    p_c_time_date->tm_mon = (p_sst->date_time.month > 0U) ? ((int)p_sst->date_time.month - 1) : 0;
    p_c_time_date->tm_year = (int)p_sst->date_time.year - 1900;

    // Ignore daylight saving for this conversion.
    p_c_time_date->tm_isdst = 0;
}

static void calc_sst(uint16_t offset, struct tm *p_c_time_date)
{
    time_t c_time_in_sec;

    c_time_in_sec = timeutil_timegm(p_c_time_date);
    c_time_in_sec -= ((time_t)offset * 60);

    // gmtime_r is thread-safe and stores the result in p_c_time_date
    if (gmtime_r(&c_time_in_sec, p_c_time_date) == NULL) {
        // Error handling: if conversion fails, do not proceed.
        return;
    }

    if (p_c_time_date->tm_isdst == 1) {
        // Daylight saving time is not used and must be removed.
        p_c_time_date->tm_hour -= 1;
        p_c_time_date->tm_isdst = 0;
    }
}

static void convert_c_time_ble_time(ble_cgms_sst_t *p_sst, const struct tm *p_c_time_date)
{
    p_sst->date_time.seconds = p_c_time_date->tm_sec;
    p_sst->date_time.minutes = p_c_time_date->tm_min;
    p_sst->date_time.hours   = p_c_time_date->tm_hour;
    p_sst->date_time.day     = p_c_time_date->tm_mday;
    p_sst->date_time.month   = p_c_time_date->tm_mon + 1;
    p_sst->date_time.year    = p_c_time_date->tm_year + 1900;
}

static uint8_t sst_encode(const ble_cgms_sst_t *p_sst, uint8_t *p_encoded_sst)
{
    uint8_t len;

    len = ble_date_time_encode_local(&p_sst->date_time, p_encoded_sst);
    p_encoded_sst[len++] = p_sst->time_zone;
    p_encoded_sst[len++] = p_sst->dst;

    return len;
}


// 基于本地时间计算 SST
static int cgm_update_sst(const uint8_t *p_data, uint16_t len)
{
    printk("Update SST\n");
    ble_cgms_sst_t sst;
    struct tm c_time_and_date;

    memset(&sst, 0, sizeof(sst));
    memset(&c_time_and_date, 0, sizeof(c_time_and_date));

    sst_decode(&sst, p_data, len);
    convert_ble_time_c_time(&sst, &c_time_and_date);
    calc_sst(m_status.time_offset, &c_time_and_date);
    convert_c_time_ble_time(&sst, &c_time_and_date);

    return cgms_sst_set(&sst);
}

int cgms_sst_set(const ble_cgms_sst_t *p_sst)
{
    uint8_t encoded[NRF_BLE_CGMS_SST_LEN];
    uint8_t len;

    if (p_sst == NULL) {
        return -EINVAL;
    }

    len = sst_encode(p_sst, encoded);
    if (len != NRF_BLE_CGMS_SST_LEN) {
        return -EINVAL;
    }

    m_sst = *p_sst;
    return 0;
}

#if 1
ssize_t cgms_read_feature(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset)
{
    uint8_t value[6];
    uint8_t value_len = cgms_encode_feature(value);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, value_len);
}

ssize_t cgms_read_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset)
{
    uint8_t value[5];
    uint8_t value_len = cgms_encode_status(value);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, value_len);
}

ssize_t cgms_read_sst(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset)
{
    uint8_t value[NRF_BLE_CGMS_SST_LEN];
    uint8_t value_len = sst_encode(&m_sst, value);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, value_len);
}

ssize_t cgms_read_srt(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset)
{
    uint8_t value[2];

    put_le16(value, m_session_run_time);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}
#endif

ssize_t cgms_write_sst(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);
    printk("buf len: %u\n", len);
    if (offset != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len != NRF_BLE_CGMS_SST_LEN) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    printk("Received SST write request: %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
           ((uint8_t *)buf)[0], ((uint8_t *)buf)[1], ((uint8_t *)buf)[2], ((uint8_t *)buf)[3],
           ((uint8_t *)buf)[4], ((uint8_t *)buf)[5], ((uint8_t *)buf)[6], ((uint8_t *)buf)[7],
           ((uint8_t *)buf)[8]);
    // 会话已开始时不允许写 SST
    if (m_session_started) {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }
    if (cgm_update_sst((const uint8_t *)buf, len) != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return len;
}
