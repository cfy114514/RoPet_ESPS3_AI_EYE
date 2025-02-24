/*
 * @Author: HoGC 
 * @Date: 2022-10-26 11:30:08 
 * @Last Modified by:   HoGC 
 * @Last Modified time: 2022-10-26 11:30:08 
 */
#ifndef __FRAME_PARSER_H__
#define __FRAME_PARSER_H__

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <stdbool.h>

#define FRAME_SWAP_16(x) ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))

#define FRAME_PARSER_HEAD        0xAA55   //0xAA55->0x55AA  性能考虑 手动交换的

typedef struct{
    uint16_t head;
    uint16_t len;
    uint16_t cmd;
}__attribute__ ((packed))frame_parser_head_t;

typedef struct{
    uint8_t sum;
}__attribute__ ((packed))frame_parser_last_t;

#define FRAME_MIN_LEN          (sizeof(frame_parser_head_t) + 2 + sizeof(frame_parser_last_t))
#define FRAME_MAX_LEN          (sizeof(frame_parser_head_t) + 512 + sizeof(frame_parser_last_t))
#define FRAME_DATA_LEN(h)      (FRAME_SWAP_16(h->len)) 

bool frame_parser_init(uint32_t size);
void frame_parser_reset(void);
uint32_t frame_parser_add_buf(uint8_t *buf, uint32_t len);
bool frame_parser_get_frame(uint8_t *frame, uint32_t *len);

#ifdef __cplusplus
}
#endif


#endif