#include <errno.h>
#include <string.h>

#include "cgms_db.h"

// typedef struct
// {
//     bool           in_use_flag;
//     ble_cgms_rec_t record;
// } database_entry_t;

// static database_entry_t m_database[CGMS_DB_MAX_RECORDS];
// static uint8_t          m_database_crossref[CGMS_DB_MAX_RECORDS];
// static uint16_t         m_num_records;

// int cgms_db_init(void)
// {
//     int i;

//     for (i = 0; i < CGMS_DB_MAX_RECORDS; i++)
//     {
//         m_database[i].in_use_flag = false;
//         m_database_crossref[i]    = 0xFF;
//     }

//     m_num_records = 0;

//     return 0;
// }
typedef struct
{
    bool           in_use_flag;
    ble_cgms_rec_t record;
} database_entry_t;

static database_entry_t m_database[CGMS_DB_MAX_RECORDS];
static uint8_t          m_database_crossref[CGMS_DB_MAX_RECORDS];
static uint16_t         m_num_records;

static void cgms_db_dump_records(const char *tag)
{
    uint16_t i;
    ble_cgms_rec_t rec;

    printk("CGMS DB DUMP [%s]: total=%u\n", tag, m_num_records);

    for (i = 0; i < m_num_records; i++)
    {
        rec = m_database[m_database_crossref[i]].record;
        printk("  idx=%u, offset=%u, glucose=%u, flags=0x%02x\n",
               i,
               rec.meas.time_offset,
               rec.meas.glucose_concentration,
               rec.meas.flags);
    }
}

/**@brief Initialize database with mock glucose records for debugging. */
static void cgms_db_init_mock_data(void)
{
#if CGMS_DB_ENABLE_MOCK_DATA
    ble_cgms_rec_t rec;
    int i;
    int32_t add_err;
    uint16_t time_offset = 0;
    uint16_t glucose_val = 150;  // Start with 150 mg/dL

    // Add 10 mock glucose records with varying values
    for (i = 0; i < 10; i++)
    {
        memset(&rec, 0, sizeof(rec));

        // Flags: indicate status annunciation is present
        rec.meas.flags = NRF_BLE_CGMS_STATUS_FLAGS_STATUS_OCT_PRESENT;

        // Glucose concentration (in mg/dL)
        // Simulate varying glucose values
        if (i % 3 == 0) {
            glucose_val += GL_CONCENTRATION_INC;     // Increase glucose
        } else {
            glucose_val -= GL_CONCENTRATION_DEC;     // Decrease glucose
        }

        // Clamp values within valid range
        if (glucose_val > MAX_GLUCOSE_CONCENTRATION) {
            glucose_val = MAX_GLUCOSE_CONCENTRATION;
        }
        if (glucose_val < MIN_GLUCOSE_CONCENTRATION) {
            glucose_val = MIN_GLUCOSE_CONCENTRATION;
        }

        rec.meas.glucose_concentration = glucose_val;

        // Time offset: increment by approximately every 5 minutes (in seconds)
        time_offset += 300;  // 5 minutes per record
        rec.meas.time_offset = time_offset;

        // Sensor status annunciation
        rec.meas.sensor_status_annunciation.warning = 0;
        rec.meas.sensor_status_annunciation.calib_temp = 0;
        rec.meas.sensor_status_annunciation.status = 0;  // No warnings

        // Optional trend and quality (not used in basic setup)
        rec.meas.trend = 0;
        rec.meas.quality = 0;

        // Add record to database
        add_err = cgms_db_record_add(&rec);
        printk("CGMS DB MOCK: add[%d] err=%d, offset=%u, glucose=%u, flags=0x%02x\n",
               i,
               add_err,
               rec.meas.time_offset,
               rec.meas.glucose_concentration,
               rec.meas.flags);
    }

    cgms_db_dump_records("mock_init");
#endif
}


int32_t cgms_db_init(void)
{
    int i;

    for (i = 0; i < CGMS_DB_MAX_RECORDS; i++)
    {
        m_database[i].in_use_flag = false;
        m_database_crossref[i]    = 0xFF;
    }

    m_num_records = 0;
    cgms_db_init_mock_data();

    return 0;
}

uint16_t cgms_db_num_records_get(void)
{
    return m_num_records;
}

int32_t cgms_db_record_get(uint8_t record_num, ble_cgms_rec_t * p_rec)
{
    if ((record_num >= m_num_records) || (m_num_records == 0))
    {
        return -1;
    }
    // copy record to the specified memory
    *p_rec = m_database[m_database_crossref[record_num]].record;

    return 0;
}

int32_t cgms_db_record_add(ble_cgms_rec_t * p_rec)
{
    int i;

    if (m_num_records == CGMS_DB_MAX_RECORDS)
    {
        return -1;
    }

    // find next available database entry
    for (i = 0; i < CGMS_DB_MAX_RECORDS; i++)
    {
        if (!m_database[i].in_use_flag)
        {
            m_database[i].in_use_flag = true;
            m_database[i].record      = *p_rec;

            m_database_crossref[m_num_records] = i;
            m_num_records++;

            return 0;
        }
    }

    return 0;
}


int32_t cgms_db_record_delete(uint8_t record_num)
{
    int i;

    if (record_num >= m_num_records)
    {
        // Deleting a non-existent record is not an error
        return 0;
    }

    // free entry
    m_database[m_database_crossref[record_num]].in_use_flag = false;

    // decrease number of records
    m_num_records--;

    // remove cross reference index
    for (i = record_num; i < m_num_records; i++)
    {
        m_database_crossref[i] = m_database_crossref[i + 1];
    }

    return 0;
}


#if 1
void put_le16(uint8_t *dst, uint16_t value)
{
	dst[0] = (uint8_t)(value & 0xFF);
	dst[1] = (uint8_t)(value >> 8);
}

uint16_t get_le16(const uint8_t *src)
{
	return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

void cgms_racp_reset_state(void)
{
	memset(&m_racp, 0, sizeof(m_racp));
}

uint8_t cgms_normalize_comm_interval(uint8_t interval)
{
	if (interval == SOCP_COMM_INTERVAL_USE_DEFAULT) {
		return GLUCOSE_MEAS_INTERVAL_MINUTES;
	}
	return interval;
}

// int cgms_record_index_offset_less_or_equal_get(uint16_t offset, uint16_t *record_num)
// {
// 	uint16_t i;

// 	if ((record_num == NULL) || (m_record_count == 0U)) {
// 		return -ENOENT;
// 	}

// 	for (i = m_record_count; i > 0U; i--) {
// 		if (m_records[i - 1U].meas.time_offset <= offset) {
// 			*record_num = (uint16_t)(i - 1U);
// 			return 0;
// 		}
// 	}

// 	return -ENOENT;
// }

// int cgms_record_index_offset_greater_or_equal_get(uint16_t offset, uint16_t *record_num)
// {
// 	uint16_t i;

// 	if ((record_num == NULL) || (m_record_count == 0U)) {
// 		return -ENOENT;
// 	}

// 	for (i = 0U; i < m_record_count; i++) {
// 		if (m_records[i].meas.time_offset >= offset) {
// 			*record_num = i;
// 			return 0;
// 		}
// 	}

// 	return -ENOENT;
// }

// void cgms_record_add(const struct cgms_record *rec)
// {
// 	if (m_record_count >= CGMS_DB_MAX_RECORDS) {
// 		memmove(&m_records[0], &m_records[1], sizeof(m_records[0]) * (CGMS_DB_MAX_RECORDS - 1));
// 		m_record_count = CGMS_DB_MAX_RECORDS - 1;
// 	}

// 	m_records[m_record_count++] = *rec;
// }

uint8_t cgms_encode_feature(uint8_t *buf)
{
	buf[0] = (uint8_t)(m_feature.feature & 0xFFU);
	buf[1] = (uint8_t)((m_feature.feature >> 8) & 0xFFU);
	buf[2] = (uint8_t)((m_feature.feature >> 16) & 0xFFU);
	buf[3] = (uint8_t)((m_feature.sample_location << 4) | (m_feature.type & 0x0F));
	buf[4] = 0xFF;
	buf[5] = 0xFF;
	return 6;
}

uint8_t cgms_encode_status(uint8_t *buf)
{
	put_le16(buf, m_status.time_offset);
	buf[2] = m_status.status.status;
	buf[3] = m_status.status.calib_temp;
	buf[4] = m_status.status.warning;
	return 5;
}

uint8_t cgms_encode_sst(uint8_t *buf)
{
	put_le16(&buf[0], m_sst.date_time.year);
	buf[2] = m_sst.date_time.month;
	buf[3] = m_sst.date_time.day;
	buf[4] = m_sst.date_time.hours;
	buf[5] = m_sst.date_time.minutes;
	buf[6] = m_sst.date_time.seconds;
	buf[7] = m_sst.time_zone;
	buf[8] = m_sst.dst;
	return 9;
}

// 在meas里实现
// uint8_t cgms_encode_measurement(const struct cgms_record *rec, uint8_t *buf)
// {
// 	uint8_t len = 2;
// 	uint8_t flags = rec->meas.flags;

// 	put_le16(&buf[len], rec->meas.glucose_concentration);
// 	len += 2;
// 	put_le16(&buf[len], rec->meas.time_offset);
// 	len += 2;

// 	if (rec->meas.sensor_status_annunciation.warning != 0U) {
// 		buf[len++] = rec->meas.sensor_status_annunciation.warning;
// 		flags |= NRF_BLE_CGMS_STATUS_FLAGS_WARNING_OCT_PRESENT;
// 	}
// 	if (rec->meas.sensor_status_annunciation.calib_temp != 0U) {
// 		buf[len++] = rec->meas.sensor_status_annunciation.calib_temp;
// 		flags |= NRF_BLE_CGMS_STATUS_FLAGS_CALTEMP_OCT_PRESENT;
// 	}
// 	if (rec->meas.sensor_status_annunciation.status != 0U) {
// 		buf[len++] = rec->meas.sensor_status_annunciation.status;
// 		flags |= NRF_BLE_CGMS_STATUS_FLAGS_STATUS_OCT_PRESENT;
// 	}

// 	buf[0] = len;
// 	buf[1] = flags;
// 	return len;
// }
#endif
