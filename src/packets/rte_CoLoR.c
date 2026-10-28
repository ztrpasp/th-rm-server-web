#include "packets/rte_CoLoR.h"
#include "commandline/cli.h"
#include "telemetry/rm_log.h"

static void show_ANN(uint8_t *buf);
static void show_GET(uint8_t *buf);
static void show_DATA(uint8_t *buf);
static void show_CONTROL(uint8_t *buf);

/* ------------------------------construction of packet---------------------------------- */

static uint16_t
calculate_cksum(uint16_t *buf, int count)
{
	uint32_t sum = 0;
	while(count > 1)
	{
		sum += *buf++;
		count -= 2;
	}
	if(count > 0)
		sum += *(uint8_t *)buf;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum>>16);
	return (uint16_t)(~sum);
}

// 1 for good, 0 for bad
static uint8_t
check_cksum(uint16_t *buf, int count)
{
	uint32_t sum = 0;
	while(count > 1)
	{
		sum += *buf++;
		count -= 2;
	}
	if(count > 0)
		sum += *(uint8_t *)buf;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum>>16);
	return (uint8_t)((sum == 0xffff) ? 1 : 0);
}

// initialize the front part of the ANN pkt
void
ANN_init(uint8_t *buf)
{
    if(!buf)
		return; // Corrected: void function should not return value

    ANN_front_t *ann_pt = (ANN_front_t *)buf;
    ann_pt->version_package = 0x71;
	ann_pt->ttl = DEFAULT_TTL;
	ann_pt->pkg_len = sizeof(ANN_front_t);
	ann_pt->checksum = 0;
	ann_pt->flag = 0;
	ann_pt->unit_px_num = 0;
}


int
ANN_update_checksum(uint8_t *buf)
{
	if(!buf)
		return -1;

	ANN_front_t *ann_pt = (ANN_front_t *)buf;
	ann_pt->checksum = 0;
	ann_pt->checksum = calculate_cksum((uint16_t *)buf, ann_pt->pkg_len);
	return 1;
}

int
ANN_check_checksum(uint8_t *buf)
{
	if(!buf)
		return -1;

	ANN_front_t *ann_pt = (ANN_front_t *)buf;
	return check_cksum((uint16_t *)buf, ann_pt->pkg_len);
}

// append a unit to ANN
void
ANN_append_unit(uint8_t *buf, uint8_t *unit)
{
	if(!buf || !unit)
		return;

	ANN_front_t *ann_pt = (ANN_front_t *) buf;
	ANN_unit_front_t *new_unit = (ANN_unit_front_t *) unit;

	// copy unit to ann tail
	rte_memcpy(buf + ann_pt->pkg_len, (uint8_t *)new_unit, new_unit->unit_len);

	ann_pt->pkg_len += new_unit->unit_len;
	ADD_ANN_UNIT_NUM(ann_pt->unit_px_num);
}

// initialize a unit, with SID and nid
void
ANN_unit_construct(uint8_t *buf, uint8_t *n_sid, uint8_t *l_sid, uint8_t *nid)
{
	if(!buf)
		return;

	ANN_unit_front_t *unit_pt = (ANN_unit_front_t *) buf;
	uint8_t offset = sizeof(ANN_unit_front_t);
	unit_pt->flag = 0;
	unit_pt->strategy_num = 0;

	// append SID and NID
	if(NULL != n_sid)
	{
		SET_ANN_UNIT_FLAG_N(unit_pt->flag);
		rte_memcpy(buf + offset, n_sid, COLOR_N_SID_LEN);
		offset += COLOR_N_SID_LEN;
	}

	if(NULL != l_sid)
	{
		SET_ANN_UNIT_FLAG_L(unit_pt->flag);
		rte_memcpy(buf + offset, l_sid, COLOR_L_SID_LEN);
		offset += COLOR_L_SID_LEN;
	}

	if(NULL != nid)
	{
		SET_ANN_UNIT_FLAG_I(unit_pt->flag);
		rte_memcpy(buf + offset, nid, COLOR_NID_LEN);
		offset += COLOR_NID_LEN;
	}

	unit_pt->unit_len = offset;
}

// append strategy unit to unit buf
void
ANN_unit_append_strategy(uint8_t *buf, uint8_t t, uint8_t l, uint8_t *v)
{
	if(!buf || l <= 0 || !v)
		return;

	ANN_unit_front_t *unit_pt = (ANN_unit_front_t *) buf;
	uint8_t offset = unit_pt->unit_len;


	*((uint8_t *)(buf + offset)) = t; // t
	*((uint8_t *)(buf + offset + 1)) = l; // l

	rte_memcpy(buf + offset + 2, v, l); // v

	unit_pt->unit_len += 2 + l;
	unit_pt->strategy_num += 1;
}

// append PX to the tail of ANN
void
ANN_append_px(uint8_t *buf, uint16_t px)
{
	if(!buf) return;

	ANN_front_t *ann_t = (ANN_front_t *)buf;
	*(uint16_t *)(buf + ann_t->pkg_len) = px;
	ann_t->pkg_len += 2;
	ADD_ANN_PX_NUM(ann_t->unit_px_num);
}

// initialize the front of the GET pkt
void
GET_init(uint8_t *buf, uint8_t *n_sid, uint8_t *l_sid, uint8_t *nid)
{
    if(!buf || !n_sid || !l_sid || !nid)
        return; // Corrected: void function should not return value

    GET_front_t *get_pt = (GET_front_t *)buf;
    get_pt->version_package = 0x72;
    get_pt->ttl = DEFAULT_TTL;
	get_pt->pkg_len = sizeof(GET_front_t);
	get_pt->checksum = 0;
	get_pt->MTU = 0;
	get_pt->PID_num = 0;
	get_pt->flag = 0;
	get_pt->Minimal_PID_CP = 0;

	rte_memcpy(get_pt->N_sid, n_sid, COLOR_N_SID_LEN);
	rte_memcpy(get_pt->L_sid, l_sid, COLOR_L_SID_LEN);
	rte_memcpy(get_pt->nid, nid, COLOR_NID_LEN);

}

// append PID to the tail of GET
void
GET_append_pid(uint8_t *buf, uint32_t pid)
{
	if(!buf)
		return;

	GET_front_t *get_pt = (GET_front_t *)buf;
	*(uint32_t *)(buf + get_pt->pkg_len) = pid;
	get_pt->pkg_len += 4;
	get_pt->PID_num += 1;
}

// append random number to the tail of GET
void GET_append_rn(uint8_t *buf, uint32_t rn)
{
	if(!buf)
		return;

	GET_front_t *get_pt = (GET_front_t *)buf;
	*(uint32_t *)(buf + get_pt->pkg_len) = rn;
	get_pt->pkg_len += 4;
}

/** 返回GET包PID序列所占字节数，并把GET包的PID序列拷贝到pids指向的空间中 */
uint32_t GET_pids(uint8_t *buf, uint8_t *pids)
{
	if(!buf)
		return 0; // Corrected: non-void function returning 0 on error

	GET_front_t *get_pt = (GET_front_t *)buf;
	uint8_t *pids_begin = buf + get_pt->pkg_len - get_pt->PID_num * 4;
	if (CHECK_GET_FLAG_R(get_pt->flag)) { // random number
		pids_begin = buf + get_pt->pkg_len - get_pt->PID_num * 4 - 4;
	}
	rte_memcpy(pids, pids_begin, get_pt->PID_num * 4);
	return get_pt->PID_num * 4;
}

uint32_t
GET_calculate_PID(uint8_t *pkt, uint32_t sk, uint16_t to_px)
{
    GET_front_t *get_pt = (GET_front_t *)pkt;
    uint8_t buf[128], digest[16], _offset;

    memset(buf, 0, 128);
    rte_memcpy(buf, get_pt->nid, 16); // nid
	rte_memcpy(buf + 16, get_pt->N_sid, 16); // n_sid
    rte_memcpy(buf + 32, get_pt->L_sid, 20); // l_sid
    // PIDo, WITHOUT OPTIONAL SEGMENT!!
	if(get_pt->PID_num > 0)
    {
		memcpy(buf + 52, pkt + get_pt->pkg_len - 4 , 4);
		_offset = 56;
	}
	else
		_offset = 52;
	rte_memcpy(buf + _offset, &sk, 4); // sk
	_offset += 4;
    rte_memcpy(buf + _offset, &to_px, 2); // px
    _offset += 2;

    // MD5 hash
    md5(buf, _offset, digest);
	// px + hash
    return (uint32_t)((uint32_t)(to_px << 16) + *(uint16_t *)(digest + 14));
}

void
GET_update_checksum(uint8_t *buf)
{
	if(!buf)
		return;

	GET_front_t *get_pt = (GET_front_t *)buf;
	get_pt->checksum = 0;
	get_pt->checksum = calculate_cksum((uint16_t *)buf, get_pt->pkg_len);
}

int
GET_check_checksum(uint8_t *buf)
{
	if(!buf)
		return -1;

	GET_front_t *get_pt = (GET_front_t *)buf;
	return check_cksum((uint16_t *)buf, get_pt->pkg_len);
}

// initialize the front of the DATA pkt
void
DATA_init(uint8_t *buf, uint8_t *n_sid, uint8_t *l_sid, uint8_t *nid)
{
    if(!buf || !n_sid || !l_sid || !nid)
        return; // Corrected: void function should not return value

    DATA_front_t *data_pt = (DATA_front_t *)buf;
    data_pt->version_package = 0x73;
    data_pt->ttl = DEFAULT_TTL;
	data_pt->pkg_len = sizeof(DATA_front_t);
	data_pt->checksum = 0;
	data_pt->header_len = data_pt->pkg_len;
	data_pt->PID_pt = 0;
	data_pt->PID_num = 0;
	data_pt->flag = 0;

	rte_memcpy(data_pt->N_sid, n_sid, COLOR_N_SID_LEN);
	rte_memcpy(data_pt->L_sid, l_sid, COLOR_L_SID_LEN);
	rte_memcpy(data_pt->nid, nid, COLOR_NID_LEN);

}

// append PID to the tail of DATA
// must consider about data behind header!
void
DATA_append_pid(uint8_t *buf, uint32_t pid)
{
	if(!buf)
		return;

	DATA_front_t *data_pt = (DATA_front_t *)buf;
	*(uint32_t *)(buf + data_pt->pkg_len) = pid;
	data_pt->pkg_len += 4;
	data_pt->header_len = data_pt->pkg_len;
	data_pt->PID_pt += 1;
	data_pt->PID_num += 1;
}

// append payload to DATA
void
DATA_append_data(uint8_t *buf, uint8_t *data, int len)
{
	if(!buf || !data || len <= 0)
		return;

	DATA_front_t *data_pt = (DATA_front_t *)buf;


	rte_memcpy(buf + data_pt->pkg_len, data, len);

	data_pt->pkg_len += len;
}

void
DATA_update_checksum(uint8_t *buf)
{
	if(!buf)
		return;

	DATA_front_t *data_pt = (DATA_front_t *)buf;
	data_pt->checksum = 0;
	data_pt->checksum = calculate_cksum((uint16_t *)buf, data_pt->header_len);
}

int
DATA_check_checksum(uint8_t *buf)
{
	if(!buf)
		return -1;

	DATA_front_t *data_pt = (DATA_front_t *)buf;
	return check_cksum((uint16_t *)buf, data_pt->header_len);
}

void
CONTROL_init(uint8_t *buf, uint8_t tag, uint16_t len, uint8_t *con_data)
{
	if(!buf)
	{
		RM_LOG_ERROR(RM_LOG_MODULE_CONTROL, RM_EVENT_PACKET_MALFORMED,
			RM_LOG_PORT_NONE, "CONTROL_init received a null output buffer");
		return;
	}
	CONTROL_front_t *con_pt = (CONTROL_front_t *)buf;
	con_pt->version_package = 0x74;
	con_pt->ttl = DEFAULT_TTL;
	con_pt->header_len = sizeof(CONTROL_front_t);
	con_pt->tag = tag;
	con_pt->len = len;

	rte_memcpy(buf + sizeof(CONTROL_front_t), con_data, len);
}

void
CONTROL_update_checksum(uint8_t *buf)
{
	if(!buf)
		return;
	CONTROL_front_t *con_pt = (CONTROL_front_t *)buf;
	con_pt->checksum = 0;
	con_pt->checksum = calculate_cksum((uint16_t *)buf, con_pt->header_len + con_pt->len);
}

int
CONTROL_check_checksum(uint8_t *buf)
{
	if(!buf)
		return -1;

	CONTROL_front_t *con_pt = (CONTROL_front_t *)buf;

	if(con_pt->checksum == 0) // no need for check
		return 1;

	return check_cksum((uint16_t *)buf, con_pt->header_len + con_pt->len);
}

/* ------------------------------tool function---------------------------------- */

// get package protocol
int
CoLoR_get_pkg_protocol(uint8_t *buf)
{
	if(!buf)
		return -1;
	else
		return GET_PROTOCOL_TYPE(buf[0]);
}

// get package type
int
CoLoR_get_pkg_type(uint8_t *buf)
{
	if(!buf)
		return -1;
	else
		return GET_PACKAGE_TYPE(buf[0]);
}

// get package length
int
CoLoR_get_pkg_length(uint8_t *buf)
{
	switch(CoLoR_get_pkg_type(buf))
	{
		case COLOR_ANN_TYPE:
		{
			return (((ANN_front_t *)buf))->pkg_len;
		}
		case COLOR_GET_TYPE:
		{
			return (((GET_front_t *)buf))->pkg_len;
		}
		case COLOR_DATA_TYPE:
		{
			return (((DATA_front_t *)buf))->pkg_len;
		}
		case COLOR_CONTROL_TYPE:
		{
			return (((CONTROL_front_t *)buf))->len + (((CONTROL_front_t *)buf))->header_len;
		}
		default: return -1;
	}
}

/* ------------------------------show function---------------------------------- */

void
show_segment(uint8_t *buf, int len)
{
	int i;
	cli_print("0x");
	for(i=0; i<len; i++)
	{
		cli_print("%x ", *(uint8_t *)(buf + i));
	}
	putchar('\n');
}

// show color package
void
CoLoR_show_pkg(uint8_t *buf)
{
	switch(CoLoR_get_pkg_type(buf))
	{
		case COLOR_ANN_TYPE:
		{
			return show_ANN(buf);
		}
		case COLOR_GET_TYPE:
		{
			return show_GET(buf);
		}
		case COLOR_DATA_TYPE:
		{
			return show_DATA(buf);
		}
		case COLOR_CONTROL_TYPE:
		{
			return show_CONTROL(buf);
		}
		default: ;
	}
}

static void
show_ANN(uint8_t *buf)
{
	if(!buf) return;

	ANN_front_t *ann_pt = (ANN_front_t *)buf;
	ANN_unit_front_t *unit_pt;

	int i, j, offset = sizeof(ANN_front_t);
	int unit_num = GET_ANN_UNIT_NUM(ann_pt->unit_px_num),
		px_num = GET_ANN_PX_NUM(ann_pt->unit_px_num);
	uint8_t s_len;

	// show the head of ANN
	cli_print("-ANN:\n\tversion_package: 0x%x\n\tttl: %d\n\tpkg_len: %d\n\tchecksum: 0x%x\n\tflag: 0x%x\n\tunit_px_num: 0x%x\n\n",
			ann_pt->version_package,
			ann_pt->ttl,
			ann_pt->pkg_len,
			ann_pt->checksum,
			ann_pt->flag,
			ann_pt->unit_px_num);

	// show the units in ANN
	for(i = 0; i < unit_num ; i++)
	{
		unit_pt = (ANN_unit_front_t *) (buf + offset);
		cli_print("\t-UNIT:\n\t\tflag: 0x%x\n\t\tunit_len: %d\n\t\tstrategy_num: %d\n",
				unit_pt->flag,
				unit_pt->unit_len,
				unit_pt->strategy_num);
		offset += sizeof(ANN_unit_front_t);

		// show sid/nid or not
		if(CHECK_ANN_UNIT_FLAG_N(unit_pt->flag))
		{
			cli_print("\t\tn_sid: ");
			show_segment(buf + offset, COLOR_N_SID_LEN);
			offset += COLOR_N_SID_LEN;
		}
		if(CHECK_ANN_UNIT_FLAG_L(unit_pt->flag))
		{
			cli_print("\t\tl_sid: ");
			show_segment(buf + offset, COLOR_L_SID_LEN);
			offset += COLOR_L_SID_LEN;
		}
		if(CHECK_ANN_UNIT_FLAG_I(unit_pt->flag))
		{
			cli_print("\t\tnid: ");
			show_segment(buf + offset, COLOR_NID_LEN);
			offset += COLOR_NID_LEN;
		}

		// show strategies in unit
		for(j=0; j < unit_pt->strategy_num; j++)
		{
			s_len = *(uint8_t *)(buf + offset + 1);
			cli_print("\t\t-S_UNIT:\n\t\t\ttag: %d\n\t\t\tlen: %d\n" ,
					*(uint8_t *)(buf + offset),
					s_len);
			cli_print("\t\t\tvalue: ");
			show_segment(buf + offset + 2, s_len);
			offset += 2 + s_len;
		}
	}

	// show px
	for(i = 0; i < px_num; i++)
	{
		cli_print("\tpx: 0x%x\n", *(uint16_t *)(buf + offset + i * 2));
	}
}

static void
show_GET(uint8_t *buf)
{
	if(!buf) return;

	int i;
	GET_front_t *get_pt = (GET_front_t *)buf;

	cli_print("GET:\n\tversion_package: 0x%x\n\tttl: %d\n\tpkg_len: %d\n\tchecksum: 0x%x\n\tMTU: %d\n\tPID_num: %d\n\tflag: 0x%x\n\tMinimal_PID_CP: %d\n",
			get_pt->version_package,
			get_pt->ttl,
			get_pt->pkg_len,
			get_pt->checksum,
			get_pt->MTU,
			get_pt->PID_num,
			get_pt->flag,
			get_pt->Minimal_PID_CP);

	cli_print("\tn_sid: ");show_segment(get_pt->N_sid, COLOR_N_SID_LEN);
	cli_print("\tl_sid: ");show_segment(get_pt->L_sid, COLOR_L_SID_LEN);
	cli_print("\tnid: ");show_segment(get_pt->nid, COLOR_NID_LEN);

	for(i=0; i < get_pt -> PID_num; i++)
	{
		cli_print("\tpid: 0x%x\n", *(uint32_t *)(buf + sizeof(GET_front_t) + i * 4));
	}

}

static void
show_DATA(uint8_t *buf)
{
	if(!buf) return;

	int i;
	DATA_front_t *data_pt = (DATA_front_t *)buf;

	cli_print("DATA:\n\tversion_package: 0x%x\n\tttl: %d\n\tpkg_len: %d\n\tchecksum: 0x%x\n\theader_len: %d\n\tPID_pt: %d\n\tPID_num: %x\n\tflag: %x\n",
			data_pt->version_package,
			data_pt->ttl,
			data_pt->pkg_len,
			data_pt->checksum,
			data_pt->header_len,
			data_pt->PID_pt,
			data_pt->PID_num,
			data_pt->flag);

	cli_print("\tn_sid: ");show_segment(data_pt->N_sid, COLOR_N_SID_LEN);
	cli_print("\tl_sid: ");show_segment(data_pt->L_sid, COLOR_L_SID_LEN);
	cli_print("\tnid: ");show_segment(data_pt->nid, COLOR_NID_LEN);

	for(i=0; i < data_pt -> PID_num; i++)
	{
		cli_print("\tpid: 0x%x\n", *(uint32_t *)(buf + sizeof(DATA_front_t) + i * 4));
	}

	cli_print("\tdata: ");show_segment(buf + data_pt->header_len,
							data_pt->pkg_len - data_pt->header_len);

}

static void
show_CONTROL(uint8_t *buf)
{
	if(!buf) return;
	CONTROL_front_t *con_pt = (CONTROL_front_t *)buf;
	cli_print("version_pack:%x\nheader_len:%x\ntag:%x\nlen:%x\n",
				con_pt->version_package,
				con_pt->header_len,
				con_pt->tag,
				con_pt->len);
}

// Constants are the integer part of the sines of integers (in radians) * 2^32.
const uint32_t k[64] = {
0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee ,
0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501 ,
0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be ,
0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821 ,
0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa ,
0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8 ,
0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed ,
0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a ,
0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c ,
0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70 ,
0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05 ,
0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665 ,
0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039 ,
0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1 ,
0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1 ,
0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };

// r specifies the per-round shift amounts
const uint32_t r[] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                      5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
                      4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                      6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

// leftrotate function definition
#define LEFTROTATE(x, c) (((x) << (c)) | ((x) >> (32 - (c))))

void
to_bytes(uint32_t val, uint8_t *bytes)
{
    bytes[0] = (uint8_t) val;
    bytes[1] = (uint8_t) (val >> 8);
    bytes[2] = (uint8_t) (val >> 16);
    bytes[3] = (uint8_t) (val >> 24);
}

uint32_t
to_int32(const uint8_t *bytes)
{
    return (uint32_t) bytes[0]
        | ((uint32_t) bytes[1] << 8)
        | ((uint32_t) bytes[2] << 16)
        | ((uint32_t) bytes[3] << 24);
}

void
md5(const uint8_t *initial_msg, size_t initial_len, uint8_t *digest)
{

    // These vars will contain the hash
    uint32_t h0, h1, h2, h3;

    // Message (to prepare)
    uint8_t *msg = NULL;

    size_t new_len, offset;
    uint32_t w[16];
    uint32_t a, b, c, d, i, f, g, temp;

    // Initialize variables - simple count in nibbles:
    h0 = 0x67452301;
    h1 = 0xefcdab89;
    h2 = 0x98badcfe;
    h3 = 0x10325476;

    //Pre-processing:
    //append "1" bit to message
    //append "0" bits until message length in bits = 448 (mod 512)
    //append length mod (2^64) to message

    for (new_len = initial_len + 1; new_len % (512/8) != 448/8; new_len++)
        ;

    msg = (uint8_t*)rte_malloc(NULL, new_len + 8, 0);
    rte_memcpy(msg, initial_msg, initial_len);
    msg[initial_len] = 0x80; // append the "1" bit; most significant bit is "first"
    for (offset = initial_len + 1; offset < new_len; offset++)
        msg[offset] = 0; // append "0" bits

    // append the len in bits at the end of the buffer.
    to_bytes(initial_len*8, msg + new_len);
    // initial_len>>29 == initial_len*8>>32, but avoids overflow.
    to_bytes(initial_len>>29, msg + new_len + 4);

    // Process the message in successive 512-bit chunks:
    //for each 512-bit chunk of message:
    for(offset=0; offset<new_len; offset += (512/8))
	{

        // break chunk into sixteen 32-bit words w[j], 0 = j = 15
        for (i = 0; i < 16; i++)
            w[i] = to_int32(msg + offset + i*4);

        // Initialize hash value for this chunk:
        a = h0;
        b = h1;
        c = h2;
        d = h3;

        // Main loop:
        for(i = 0; i<64; i++)
		{

            if (i < 16)
			{
                f = (b & c) | ((~b) & d);
                g = i;
            }
			else if (i < 32)
			{
                f = (d & b) | ((~d) & c);
                g = (5*i + 1) % 16;
            }
			else if (i < 48)
			{
                f = b ^ c ^ d;
                g = (3*i + 5) % 16;
            }
			else
			{
                f = c ^ (b | (~d));
                g = (7*i) % 16;
            }

            temp = d;
            d = c;
            c = b;
            b = b + LEFTROTATE((a + f + k[i] + w[g]), r[i]);
            a = temp;

        }

        // Add this chunk's hash to result so far:
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;

    }

    // cleanup
    rte_free(msg);

    //var char digest[16] := h0 append h1 append h2 append h3 //(Output is in little-endian)
    to_bytes(h0, digest);
    to_bytes(h1, digest + 4);
    to_bytes(h2, digest + 8);
    to_bytes(h3, digest + 12);
}
