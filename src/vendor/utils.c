#include "vendor/utils.h"

uint8_t _to_hex(char str)
{
    if(str >= '0' && str <= '9')
        return str - '0';
    else if(str >= 'a' && str <= 'f')
        return str - 'a' + 10;
    else if(str >= 'A' && str <= 'F')
        return str - 'A' + 10;
    else
        return 0;
}

// 0x12345678 10
// 0x12345 7 
// 0x000000000000
int str_to_hex(char *str, uint8_t *buf, int buf_byte_len)
{
    if(!str || !buf)
        return -1;
    bool flag = false;
    int i, str_byte_len, start_point;
    
    // check hex format
    if(str[0]!='0'||str[1]!='x')
        return -1;

    for(i = 0; str[i] != '\0'; i++)
        continue;
    if(i%2 == 1)
    {
        i++;
        flag = true;
    }
    str_byte_len = (i - 2)/2;
    start_point = buf_byte_len - str_byte_len;

    if(start_point < 0)
        return -1;

    memset(buf, 0, buf_byte_len);
    if(!flag)
    {
        for(i = 0; i < str_byte_len; i++)
        {
            buf[start_point + i] = _to_hex(str[2*i+2]) * 16 +
                    _to_hex(str[2*i+3]);
        }
    }
    else
    {
        buf[start_point] = _to_hex(str[2]);
        for(i = 1; i < str_byte_len; i++)
            {
                buf[start_point + i] = _to_hex(str[2*i+1]) * 16 +
                        _to_hex(str[2*i+2]);
            }
    }
    return 0;
}

uint32_t str_to_ipaddr(char *str)
{
    if(!str)
        return 0;
    int buf[4];
    if(4 != sscanf(str, "%d.%d.%d.%d", buf, buf + 1, buf + 2, buf + 3))
        return 0;
    return (uint32_t)((buf[0]<<24) + (buf[1]<<16) + (buf[2]<<8) + (buf[3]));
}