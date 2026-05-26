/**
 * @file        gcode_parser.c
 * @brief       G代码解析器源文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        实现G代码的解析功能，支持标准G代码指令的解析
 *              解析器采用状态机模式，支持绝对/增量坐标、公制/英制单位
 */

#include "gcode_parser.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

/** 全局解析器状态 */
static gcode_state_t g_state;

/** 毫米每英寸转换常数 */
static const float MM_PER_INCH = 25.4f;

/**
 * @brief  初始化G代码解析器
 */
void gcode_parser_init(void)
{
    memset(&g_state, 0, sizeof(gcode_state_t));
    g_state.motion = GCODE_MOTION_SEEK;                   /**< 默认快速定位模式 */
    g_state.distance_mode = GCODE_DISTANCE_ABSOLUTE;      /**< 默认绝对坐标 */
    g_state.units = GCODE_UNITS_MM;                      /**< 默认公制单位 */
    g_state.plane_select = GCODE_PLANE_XY;                /**< 默认XY平面 */
    g_state.feed_rate = 100.0f;                           /**< 默认进给速率100mm/min */
}

/**
 * @brief       跳过空白字符
 * @param       ptr: 字符串指针引用
 */
static void skip_whitespace(const char **ptr)
{
    while (**ptr && isspace((unsigned char)**ptr)) {
        (*ptr)++;
    }
}

/**
 * @brief       解析浮点数
 * @param       ptr: 字符串指针引用
 * @param       value: 解析结果存储指针
 * @retval      是否成功
 */
static bool parse_number(const char **ptr, float *value)
{
    skip_whitespace(ptr);

    if (!**ptr) return false;

    char *end;
    *value = strtof(*ptr, &end);
    if (end == *ptr) return false;

    *ptr = end;
    return true;
}

/**
 * @brief       解析整数
 * @param       ptr: 字符串指针引用
 * @param       value: 解析结果存储指针
 * @retval      是否成功
 */
static bool parse_int(const char **ptr, int32_t *value)
{
    skip_whitespace(ptr);

    if (!**ptr) return false;

    char *end;
    *value = strtol(*ptr, &end, 10);
    if (end == *ptr) return false;

    *ptr = end;
    return true;
}

/**
 * @brief       折叠处理一行G代码
 * @param       line: 输入行
 * @note        移除注释、大写字母、移除多余空格
 */
static void collapse_line(char *line)
{
    char *out = line;
    char *in = line;
    bool in_comment = false;

    while (*in) {
        /** 处理注释开始 */
        if (*in == '(' || *in == ';') {
            in_comment = true;
        }

        /** 复制非注释字符 */
        if (!in_comment) {
            if (!isspace((unsigned char)*in)) {
                *out++ = toupper((unsigned char)*in);
            }
        }

        /** 处理注释结束 */
        if (*in == ')' && in_comment) {
            in_comment = false;
        }

        in++;
    }

    *out = '\0';
}

/**
 * @brief       解析一行G代码
 * @param       line: 输入的G代码字符串
 * @param       block: 解析结果存储结构体
 * @retval      解析错误码
 */
gcode_error_t gcode_parse_line(const char *line, gcode_block_t *block)
{
    if (!line || !block) {
        return GCODE_ERROR_PARAM;
    }

    /** 复制并预处理输入行 */
    char buffer[256];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    /** 折叠处理(移除注释、大写等) */
    collapse_line(buffer);

    /** 初始化解析块 */
    memset(block, 0, sizeof(gcode_block_t));

    /** 继承当前状态 */
    block->motion = g_state.motion;
    block->distance_mode = g_state.distance_mode;
    block->units = g_state.units;
    block->plane_select = g_state.plane_select;
    block->feed_rate = g_state.feed_rate;

    memcpy(block->xyz, g_state.position, sizeof(g_state.position));

    char *ptr = buffer;
    bool has_motion = false;

    /** 逐字符解析 */
    while (*ptr) {
        skip_whitespace((const char**)&ptr);

        if (!*ptr) break;

        /** 读取字母码 */
        char letter = *ptr++;
        float value;

        /** 读取数值 */
        if (!parse_number((const char**)&ptr, &value)) {
            return GCODE_ERROR_SYNTAX;
        }

        /** 根据字母码处理不同参数 */
        switch (letter) {
            case 'G': {
                int g_code = (int)value;
                int mantissa = (int)((value - g_code) * 10.0f + 0.5f);

                /** 处理G代码指令 */
                switch (g_code) {
                    case 0:
                        block->motion = GCODE_MOTION_SEEK;
                        block->is_rapid = true;
                        has_motion = true;
                        break;

                    case 1:
                        block->motion = GCODE_MOTION_LINEAR;
                        has_motion = true;
                        break;

                    case 2:
                        block->motion = GCODE_MOTION_CW_ARC;
                        block->is_arc = true;
                        block->arc_cw = true;
                        has_motion = true;
                        break;

                    case 3:
                        block->motion = GCODE_MOTION_CCW_ARC;
                        block->is_arc = true;
                        block->arc_cw = false;
                        has_motion = true;
                        break;

                    case 4:
                        block->is_dwell = true;
                        break;

                    case 17:
                        block->plane_select = GCODE_PLANE_XY;
                        break;

                    case 18:
                        block->plane_select = GCODE_PLANE_ZX;
                        break;

                    case 19:
                        block->plane_select = GCODE_PLANE_YZ;
                        break;

                    case 20:
                        block->units = GCODE_UNITS_INCH;
                        break;

                    case 21:
                        block->units = GCODE_UNITS_MM;
                        break;

                    case 90:
                        block->distance_mode = GCODE_DISTANCE_ABSOLUTE;
                        break;

                    case 91:
                        block->distance_mode = GCODE_DISTANCE_INCREMENTAL;
                        break;

                    default:
                        break;
                }
                break;
            }

            case 'M': {
                int m_code = (int)value;
                switch (m_code) {
                    case 0:
                    case 1:
                    case 2:
                    case 30:
                        break;
                    default:
                        break;
                }
                break;
            }

            case 'X':
                /** 处理X坐标 */
                if (block->units == GCODE_UNITS_INCH) {
                    value *= MM_PER_INCH;
                }
                if (block->distance_mode == GCODE_DISTANCE_INCREMENTAL) {
                    block->xyz[0] = g_state.position[0] + value;
                } else {
                    block->xyz[0] = value;
                }
                block->has_x = true;
                break;

            case 'Y':
                /** 处理Y坐标 */
                if (block->units == GCODE_UNITS_INCH) {
                    value *= MM_PER_INCH;
                }
                if (block->distance_mode == GCODE_DISTANCE_INCREMENTAL) {
                    block->xyz[1] = g_state.position[1] + value;
                } else {
                    block->xyz[1] = value;
                }
                block->has_y = true;
                break;

            case 'Z':
                /** 处理Z坐标 */
                if (block->units == GCODE_UNITS_INCH) {
                    value *= MM_PER_INCH;
                }
                if (block->distance_mode == GCODE_DISTANCE_INCREMENTAL) {
                    block->xyz[2] = g_state.position[2] + value;
                } else {
                    block->xyz[2] = value;
                }
                block->has_z = true;
                break;

            case 'I':
                if (block->units == GCODE_UNITS_INCH) {
                    value *= MM_PER_INCH;
                }
                block->ijk[0] = value;
                block->has_i = true;
                break;

            case 'J':
                if (block->units == GCODE_UNITS_INCH) {
                    value *= MM_PER_INCH;
                }
                block->ijk[1] = value;
                block->has_j = true;
                break;

            case 'K':
                if (block->units == GCODE_UNITS_INCH) {
                    value *= MM_PER_INCH;
                }
                block->ijk[2] = value;
                block->has_k = true;
                break;

            case 'F':
                if (block->units == GCODE_UNITS_INCH) {
                    value *= MM_PER_INCH;
                }
                block->feed_rate = value;
                block->has_feed_rate = true;
                break;

            case 'S':
                block->spindle_speed = value;
                block->has_spindle = true;
                break;

            case 'P':
                if (block->is_dwell) {
                    block->dwell_seconds = value / 1000.0f;
                }
                break;

            case 'N':
                block->line_number = (int32_t)value;
                break;

            default:
                break;
        }
    }

    /** 验证进给速率 */
    if (has_motion && !block->is_rapid && !block->is_dwell) {
        if (!block->has_feed_rate && g_state.feed_rate <= 0) {
            return GCODE_ERROR_NO_FEEDRATE;
        }
    }

    return GCODE_OK;
}

/**
 * @brief       更新解析器状态
 * @param       block: G代码块
 */
void gcode_update_state(const gcode_block_t *block)
{
    if (!block) return;

    /** 更新坐标位置 */
    if (block->has_x) g_state.position[0] = block->xyz[0];
    if (block->has_y) g_state.position[1] = block->xyz[1];
    if (block->has_z) g_state.position[2] = block->xyz[2];

    /** 更新进给速率 */
    if (block->has_feed_rate) {
        g_state.feed_rate = block->feed_rate;
    }

    /** 更新模式设置 */
    g_state.motion = block->motion;
    g_state.distance_mode = block->distance_mode;
    g_state.units = block->units;
    g_state.plane_select = block->plane_select;
}

/**
 * @brief       获取当前解析器状态
 * @retval      状态结构体指针
 */
const gcode_state_t* gcode_get_state(void)
{
    return &g_state;
}

/**
 * @brief       设置当前位置
 * @param       x,y,z: 坐标值
 */
void gcode_set_position(float x, float y, float z)
{
    g_state.position[0] = x;
    g_state.position[1] = y;
    g_state.position[2] = z;
}

/**
 * @brief       获取错误信息字符串
 * @param       err: 错误码
 * @retval      错误信息字符串
 */
const char* gcode_error_string(gcode_error_t err)
{
    switch (err) {
        case GCODE_OK: return "OK";
        case GCODE_ERROR_SYNTAX: return "Syntax error";
        case GCODE_ERROR_UNSUPPORTED: return "Unsupported command";
        case GCODE_ERROR_PARAM: return "Parameter error";
        case GCODE_ERROR_NO_FEEDRATE: return "No feed rate specified";
        case GCODE_ERROR_NO_AXIS: return "No axis specified";
        default: return "Unknown error";
    }
}
