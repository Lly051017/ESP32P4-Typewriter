/**
 * @file        gcode_parser.h
 * @brief       G代码解析器头文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        提供G代码的解析功能，支持标准G代码指令的解析
 *              支持G0/G1/G2/G3等运动指令以及坐标系统设置
 */

#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GCODE_MAX_AXES 3        /**< 最大轴数 */
#define GCODE_MAX_PARAMS 16    /**< 最大参数数量 */

/**
 * @brief G代码运动模式枚举
 */
typedef enum {
    GCODE_MOTION_SEEK = 0,      /**< G0 - 快速定位(空运行) */
    GCODE_MOTION_LINEAR = 1,    /**< G1 - 线性进给 */
    GCODE_MOTION_CW_ARC = 2,    /**< G2 - 顺时针圆弧 */
    GCODE_MOTION_CCW_ARC = 3,   /**< G3 - 逆时针圆弧 */
    GCODE_MOTION_NONE = 80,     /**< G80 - 取消运动模式 */
} gcode_motion_t;

/**
 * @brief 距离模式枚举
 */
typedef enum {
    GCODE_DISTANCE_ABSOLUTE = 0,    /**< G90 - 绝对坐标模式 */
    GCODE_DISTANCE_INCREMENTAL = 1, /**< G91 - 增量坐标模式 */
} gcode_distance_t;

/**
 * @brief 单位模式枚举
 */
typedef enum {
    GCODE_UNITS_MM = 0,     /**< G21 - 公制单位(毫米) */
    GCODE_UNITS_INCH = 1,   /**< G20 - 英制单位(英寸) */
} gcode_units_t;

/**
 * @brief 平面选择枚举
 */
typedef enum {
    GCODE_PLANE_XY = 0,     /**< G17 - XY平面 */
    GCODE_PLANE_ZX = 1,     /**< G18 - ZX平面 */
    GCODE_PLANE_YZ = 2,     /**< G19 - YZ平面 */
} gcode_plane_t;

/**
 * @brief G代码块结构体
 * @note  存储解析后的单行G代码信息
 */
typedef struct {
    float xyz[GCODE_MAX_AXES];      /**< X/Y/Z坐标值 */
    float ijk[3];                   /**< 圆弧参数I/J/K */
    float feed_rate;                /**< 进给速率 */
    float spindle_speed;            /**< 主轴速度 */
    float dwell_seconds;            /**< 延时时间(秒) */
    int32_t line_number;            /**< 行号 */
    uint8_t motion;                 /**< 运动模式 */
    uint8_t distance_mode;           /**< 距离模式 */
    uint8_t units;                  /**< 单位模式 */
    uint8_t plane_select;           /**< 平面选择 */
    bool has_x, has_y, has_z;       /**< 坐标存在标志 */
    bool has_i, has_j, has_k;       /**< 圆弧参数存在标志 */
    bool has_feed_rate;             /**< 进给速率存在标志 */
    bool has_spindle;               /**< 主轴速度存在标志 */
    bool is_rapid;                  /**< 快速移动标志 */
    bool is_dwell;                  /**< 延时标志 */
    bool is_arc;                    /**< 圆弧运动标志 */
    bool arc_cw;                    /**< 顺时针圆弧标志 */
} gcode_block_t;

/**
 * @brief G代码状态结构体
 * @note  存储解析器的当前状态
 */
typedef struct {
    float position[GCODE_MAX_AXES];  /**< 当前坐标位置 */
    float feed_rate;                 /**< 当前进给速率 */
    uint8_t motion;                 /**< 当前运动模式 */
    uint8_t distance_mode;          /**< 当前距离模式 */
    uint8_t units;                  /**< 当前单位模式 */
    uint8_t plane_select;           /**< 当前平面选择 */
} gcode_state_t;

/**
 * @brief G代码解析错误枚举
 */
typedef enum {
    GCODE_OK = 0,                 /**< 解析成功 */
    GCODE_ERROR_SYNTAX,           /**< 语法错误 */
    GCODE_ERROR_UNSUPPORTED,      /**< 不支持的指令 */
    GCODE_ERROR_PARAM,            /**< 参数错误 */
    GCODE_ERROR_NO_FEEDRATE,      /**< 未指定进给速率 */
    GCODE_ERROR_NO_AXIS,          /**< 未指定坐标轴 */
} gcode_error_t;

/* 函数声明 */

/**
 * @brief  初始化G代码解析器
 */
void gcode_parser_init(void);

/**
 * @brief  解析一行G代码
 * @param  line: 输入的G代码字符串
 * @param  block: 解析结果存储结构体
 * @retval 解析错误码
 */
gcode_error_t gcode_parse_line(const char *line, gcode_block_t *block);

/**
 * @brief  更新解析器状态
 * @param  block: G代码块
 */
void gcode_update_state(const gcode_block_t *block);

/**
 * @brief  获取当前解析器状态
 * @retval 状态结构体指针
 */
const gcode_state_t* gcode_get_state(void);

/**
 * @brief  设置当前位置
 * @param  x,y,z: 坐标值
 */
void gcode_set_position(float x, float y, float z);

/**
 * @brief  获取错误信息字符串
 * @param  err: 错误码
 * @retval 错误信息字符串
 */
const char* gcode_error_string(gcode_error_t err);

#ifdef __cplusplus
}
#endif

#endif
