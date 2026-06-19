package com.example.typewriterandroid.gcode

import android.content.Context
import android.net.Uri
import java.io.File
import java.io.InputStream
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/**
 * GCode 指令构建器 —— Kotlin `object` 单例，直接 import 调用，无需注册。
 *
 * 对应 Qt 版 `class GCodeBuilder` + `Q_INVOKABLE`，
 * Compose UI 中直接 `GCodeBuilder.unitMM()` 即可。
 */
object GCodeBuilder {

    // =========================================================================
    // 设置类
    // =========================================================================

    fun unitMM()   = "G21"
    fun unitInch() = "G20"
    fun absolute() = "G90"
    fun relative() = "G91"
    fun planeXY()  = "G17"
    fun planeZX()  = "G18"
    fun planeYZ()  = "G19"

    // =========================================================================
    // 抬 / 落笔
    // =========================================================================

    fun penUp()                  = "G0 Z5"
    fun penDown(feed: Int = 1000) = "G1 Z0 F$feed"

    // =========================================================================
    // 运动
    // =========================================================================

    fun rapid(x: Double, y: Double) =
        "G0 X${x.f1()} Y${y.f1()}"

    fun linear(x: Double, y: Double, feed: Int) =
        "G1 X${x.f1()} Y${y.f1()} F$feed"

    fun arcCW(x: Double, y: Double, i: Double, j: Double, feed: Int) =
        "G2 X${x.f1()} Y${y.f1()} I${i.f1()} J${j.f1()} F$feed"

    fun arcCCW(x: Double, y: Double, i: Double, j: Double, feed: Int) =
        "G3 X${x.f1()} Y${y.f1()} I${i.f1()} J${j.f1()} F$feed"

    // =========================================================================
    // 其他
    // =========================================================================

    fun dwell(seconds: Double) = "G4 P$seconds"
    fun stop()                 = "M0"
    fun home()                 = "G0 X0 Y0"

    // =========================================================================
    // 预设图案
    // =========================================================================

    /** 正方形：从左下角 (x,y) 开始，边长 side */
    fun square(x: Double, y: Double, side: Double, feed: Int): List<String> =
        listOf(
            penUp(), rapid(x, y), penDown(feed),
            linear(x + side, y, feed),
            linear(x + side, y + side, feed),
            linear(x, y + side, feed),
            linear(x, y, feed),
            penUp()
        )

    /** 近圆：圆心 (cx,cy)，半径 r，segments 越大越圆 */
    fun circleApprox(
        cx: Double, cy: Double, r: Double,
        segments: Int = 36,
        feed: Int
    ): List<String> {
        val cmds = mutableListOf<String>()
        val startX = cx + r
        val startY = cy
        cmds.add(penUp())
        cmds.add(rapid(startX, startY))
        cmds.add(penDown(feed))
        for (i in 1..segments) {
            val angle = 2.0 * PI * i / segments
            cmds.add(linear(cx + r * cos(angle), cy + r * sin(angle), feed))
        }
        cmds.add(penUp())
        return cmds
    }

    /** 三角形：三个顶点依次连线 */
    fun triangle(
        x1: Double, y1: Double,
        x2: Double, y2: Double,
        x3: Double, y3: Double,
        feed: Int
    ): List<String> = listOf(
        penUp(), rapid(x1, y1), penDown(feed),
        linear(x2, y2, feed),
        linear(x3, y3, feed),
        linear(x1, y1, feed),
        penUp()
    )

    /**
     * 大写字母 A —— 两笔斜线 + 中间横线。
     *
     * @param x      左下角 X
     * @param y      左下角 Y
     * @param height 字母高度 (mm)，宽度 ≈ height × 0.6
     * @param feed   进给速度
     */
    fun letterA(x: Double, y: Double, height: Double, feed: Int): List<String> {
        val w = height * 0.6
        val topX = x + w / 2
        val topY = y + height
        val rightX = x + w
        val crossY = y + height * 0.4
        val crossLeftX = x + w * 0.2
        val crossRightX = x + w * 0.8
        return listOf(
            penUp(), rapid(x, y), penDown(feed),         // 起点
            linear(topX, topY, feed),                     // 左斜线 → 顶
            linear(rightX, y, feed),                      // 右斜线 → 右下
            penUp(),
            rapid(crossLeftX, crossY), penDown(feed),     // 横线左端
            linear(crossRightX, crossY, feed),            // 横线
            penUp()
        )
    }

    /**
     * 大写字母 B —— 左竖线 + 上下两个半圆（用多段直线逼近圆弧）。
     *
     * @param x      左下角 X
     * @param y      左下角 Y
     * @param height 字母高度 (mm)，宽度 ≈ height × 0.5
     * @param feed   进给速度
     */
    fun letterB(x: Double, y: Double, height: Double, feed: Int): List<String> {
        val w = height * 0.5
        val topY = y + height
        val midY = y + height / 2

        // 上半圆：从竖线顶部开始，用 8 段直线逼近顺时针半圆
        val upperR = height * 0.25
        val upperCY = y + height * 0.75
        val upperPts = (0..8).map { i ->
            val angle = Math.toRadians(270.0 - i * 180.0 / 8) // 从顶部顺时针
            Pair(x + upperR + upperR * kotlin.math.cos(angle),
                upperCY + upperR * kotlin.math.sin(angle))
        }

        // 下半圆：从竖线中部开始，用 8 段直线逼近顺时针半圆
        val lowerR = height * 0.25
        val lowerCY = y + height * 0.25
        val lowerPts = (0..8).map { i ->
            val angle = Math.toRadians(270.0 - i * 180.0 / 8)
            Pair(x + lowerR + lowerR * kotlin.math.cos(angle),
                lowerCY + lowerR * kotlin.math.sin(angle))
        }

        val cmds = mutableListOf<String>()
        cmds.add(penUp())
        cmds.add(rapid(x, y))
        cmds.add(penDown(feed))
        cmds.add(linear(x, topY, feed))           // 左竖线 ↑

        // 上半圆
        for ((px, py) in upperPts) {
            cmds.add(linear(px, py, feed))
        }

        // 下半圆
        for ((px, py) in lowerPts) {
            cmds.add(linear(px, py, feed))
        }

        cmds.add(penUp())
        return cmds
    }

    /**
     * 五角星 —— 5 个外顶点 + 5 个内顶点交替连线。
     *
     * 外顶点半径 = size，内顶点半径 ≈ size × 0.38
     *
     * @param cx    中心 X
     * @param cy    中心 Y
     * @param size  外接圆半径 (mm)
     * @param feed  进给速度
     */
    fun star(cx: Double, cy: Double, size: Double, feed: Int): List<String> {
        val outerR = size
        val innerR = size * 0.38
        val cmds = mutableListOf<String>()

        // 计算 10 个顶点坐标（从顶部外顶点开始，顺时针）
        for (i in 0 until 5) {
            // 外顶点 (0°, 72°, 144°, 216°, 288°) → 偏移 -90° 让第一个在正上方
            val outerAngle = Math.toRadians(i * 72.0 - 90.0)
            val ox = cx + outerR * kotlin.math.cos(outerAngle)
            val oy = cy + outerR * kotlin.math.sin(outerAngle)

            // 内顶点 (36°, 108°, 180°, 252°, 324°)
            val innerAngle = Math.toRadians(i * 72.0 + 36.0 - 90.0)
            val ix = cx + innerR * kotlin.math.cos(innerAngle)
            val iy = cy + innerR * kotlin.math.sin(innerAngle)

            if (i == 0) {
                cmds.add(penUp())
                cmds.add(rapid(ox, oy))
                cmds.add(penDown(feed))
            } else {
                cmds.add(linear(ox, oy, feed))
            }
            cmds.add(linear(ix, iy, feed))
        }
        // 回到起点闭合
        val firstAngle = Math.toRadians(-90.0)
        val firstX = cx + outerR * kotlin.math.cos(firstAngle)
        val firstY = cy + outerR * kotlin.math.sin(firstAngle)
        cmds.add(linear(firstX, firstY, feed))
        cmds.add(penUp())
        return cmds
    }

    /**
     * 快捷方式：根据 type 字符串生成对应图案的 GCode 列表。
     *
     * @param type  "letterA" | "letterB" | "star"
     * @param x     起点 X (mm)
     * @param y     起点 Y (mm)
     * @param size  尺寸 (mm) —— letterA/B 为高度，star 为外接圆半径
     * @param feed  进给速度
     * @return GCode 指令行列表
     */
    fun preset(type: String, x: Double, y: Double, size: Double, feed: Int): List<String> =
        when (type) {
            "letterA" -> letterA(x, y, size, feed)
            "letterB" -> letterB(x, y, size, feed)
            "star"    -> star(x + size / 2, y + size / 2, size / 2, feed)
            else      -> emptyList()
        }

    // =========================================================================
    // 文件读取（磁盘路径 + Android Uri）
    // =========================================================================

    /** 从 InputStream 读取 GCode 行，跳过空行和注释（; / ( )）。 */
    fun readGCodeStream(inputStream: InputStream): List<String> =
        inputStream.bufferedReader().readLines()
            .map { it.trim() }
            .filter { it.isNotEmpty() }
            .filter { !it.startsWith(";") && !it.startsWith("(") }

    /** 读取 .gcode / .nc 文件（磁盘路径）。 */
    fun readGCodeFile(filePath: String): List<String> =
        File(filePath).inputStream().use { readGCodeStream(it) }

    /**
     * 从 Android Uri 读取 GCode 文件（适配系统文件选择器返回的 content:// URI）。
     *
     * @param context Android Context（Compose 中通过 LocalContext.current 获取）
     * @param uri     系统文件选择器返回的 content URI
     * @return GCode 指令行列表，读取失败返回空列表
     */
    fun readGCodeUri(context: Context, uri: Uri): List<String> {
        return try {
            context.contentResolver.openInputStream(uri)?.use { stream ->
                readGCodeStream(stream)
            } ?: emptyList()
        } catch (e: Exception) {
            emptyList()
        }
    }

    // =========================================================================
    // 内部：数值格式化
    // =========================================================================

    private fun Double.f1() = "%.1f".format(this)
}
