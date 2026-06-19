package com.example.typewriterandroid.ui.screen

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.typewriterandroid.gcode.GCodeBuilder
import com.example.typewriterandroid.tcp.TcpClient
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

// =========================================================================
// 数据类
// =========================================================================

/**
 * 预设图案定义。
 *
 * @param name 显示名称
 * @param type 内部标识，用于 GCodeBuilder 方法分发
 */
private data class Preset(val name: String, val type: String)

// =========================================================================
// 图标绘制
// =========================================================================

/**
 * 预设图案的几何小图标，Canvas 手绘。
 *
 * 支持的 type：
 *  - square  → 空心矩形
 *  - circle  → 空心圆
 *  - triangle → 空心三角形
 *  - star    → 空心五角星
 *  - letterA → 大写 A 线稿
 *  - letterB → 大写 B 线稿
 */
@Composable
fun PresetIcon(type: String, modifier: Modifier = Modifier) {
    val tint = Color.DarkGray

    // 字母直接放大显示，不手绘
    if (type == "letterA" || type == "letterB") {
        Text(
            text = if (type == "letterA") "A" else "B",
            fontSize = 28.sp,
            color = tint,
            modifier = modifier.size(36.dp)
        )
        return
    }

    Canvas(modifier = modifier.size(36.dp)) {
        val stroke = Stroke(2.dp.toPx())
        val r = size.minDimension / 2 - 4.dp.toPx()
        val cx = size.width / 2
        val cy = size.height / 2

        when (type) {
            "square" -> {
                val half = r * 0.7f
                drawRect(
                    color = tint,
                    topLeft = Offset(cx - half, cy - half),
                    size = Size(half * 2, half * 2),
                    style = stroke
                )
            }

            "circle" -> {
                drawCircle(
                    color = tint,
                    radius = r * 0.7f,
                    center = Offset(cx, cy),
                    style = stroke
                )
            }

            "triangle" -> {
                val path = Path().apply {
                    moveTo(cx, cy - r * 0.7f)
                    lineTo(cx + r * 0.7f, cy + r * 0.7f)
                    lineTo(cx - r * 0.7f, cy + r * 0.7f)
                    close()
                }
                drawPath(path, color = tint, style = stroke)
            }

            "star" -> {
                val path = Path()
                val outerR = r * 0.7f
                val innerR = outerR * 0.4f
                for (i in 0 until 5) {
                    val outerAngle = (i * 72.0 - 90.0) * (PI.toFloat() / 180f)
                    val innerAngle = ((i * 72.0) + 36.0 - 90.0) * (PI.toFloat() / 180f)
                    val ox = cx + outerR * cos(outerAngle).toFloat()
                    val oy = cy + outerR * sin(outerAngle).toFloat()
                    val ix = cx + innerR * cos(innerAngle).toFloat()
                    val iy = cy + innerR * sin(innerAngle).toFloat()
                    if (i == 0) path.moveTo(ox, oy)
                    else path.lineTo(ox, oy)
                    path.lineTo(ix, iy)
                }
                path.close()
                drawPath(path, color = tint, style = stroke)
            }
        }
    }
}

// =========================================================================
// 预设图案页面
// =========================================================================

/**
 * 预设图案页面 —— 图案选择 Grid + 参数设置 + 一键发送。
 *
 * ## 布局（从上到下）
 *  1. 标题
 *  2. 3 列图案 Grid，选中项高亮
 *  3. 参数面板：起点 X / 起点 Y / 尺寸 / 速度
 *  4. 发送按钮（全宽）
 *
 * ## 已支持的图案
 *  - 正方形 → [GCodeBuilder.square]
 *  - 圆形   → [GCodeBuilder.circleApprox]
 *  - 三角形 → [GCodeBuilder.triangle]
 *  - 字母 A / 字母 B / 五角星 → 预留（待实现笔画路径）
 *
 * @param tcpClient TCP 客户端，用于批量发送指令
 * @param modifier  根修饰符
 */
@Composable
fun PresetScreen(
    tcpClient: TcpClient,
    modifier: Modifier = Modifier
) {
    val presets = listOf(
        Preset("正方形", "square"),
        Preset("圆形",   "circle"),
        Preset("三角形", "triangle"),
        Preset("字母 A", "letterA"),
        Preset("字母 B", "letterB"),
        Preset("五角星", "star"),
    )

    var selected by remember { mutableStateOf(presets.first()) }
    var startX by remember { mutableStateOf("50") }
    var startY by remember { mutableStateOf("50") }
    var size by remember { mutableStateOf("50") }
    var feed by remember { mutableStateOf("1000") }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        // ---- 标题 ----
        Text("预设图案", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(12.dp))

        // ---- 图案 Grid ----
        LazyVerticalGrid(
            columns = GridCells.Fixed(3),
            modifier = Modifier.height(200.dp)
        ) {
            items(presets) { preset ->
                Card(
                    modifier = Modifier
                        .padding(4.dp)
                        .clickable { selected = preset },
                    colors = CardDefaults.cardColors(
                        containerColor = if (selected == preset)
                            MaterialTheme.colorScheme.primaryContainer
                        else MaterialTheme.colorScheme.surface
                    )
                ) {
                    Column(
                        Modifier.padding(8.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        PresetIcon(
                            type = preset.type,
                            modifier = Modifier.size(36.dp)
                        )
                        Spacer(Modifier.height(4.dp))
                        Text(preset.name, fontSize = 12.sp)
                    }
                }
            }
        }

        Spacer(Modifier.height(12.dp))

        // ---- 参数面板 ----
        OutlinedTextField(
            value = startX,
            onValueChange = { startX = it },
            label = { Text("起点 X") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(
            value = startY,
            onValueChange = { startY = it },
            label = { Text("起点 Y") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(
            value = size,
            onValueChange = { size = it },
            label = { Text("尺寸") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(
            value = feed,
            onValueChange = { feed = it },
            label = { Text("速度") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(Modifier.height(12.dp))

        // ---- 发送按钮 ----
        Button(
            onClick = {
                val x = startX.toDoubleOrNull() ?: 50.0
                val y = startY.toDoubleOrNull() ?: 50.0
                val s = size.toDoubleOrNull() ?: 50.0
                val f = feed.toIntOrNull() ?: 1000
                val cmds = when (selected.type) {
                    "square"   -> GCodeBuilder.square(x, y, s, f)
                    "circle"   -> GCodeBuilder.circleApprox(x + s, y + s, s / 2, 36, f)
                    "triangle" -> GCodeBuilder.triangle(x, y, x + s, y, x + s / 2, y + s, f)
                    "letterA"  -> GCodeBuilder.letterA(x, y, s, f)
                    "letterB"  -> GCodeBuilder.letterB(x, y, s, f)
                    "star"     -> GCodeBuilder.star(x + s / 2, y + s / 2, s / 2, f)
                    else -> emptyList()
                }
                if (cmds.isNotEmpty()) {
                    tcpClient.sendCommands(cmds)
                }
            },
            modifier = Modifier.fillMaxWidth()
        ) {
            Text("发送")
        }
    }
}

// =========================================================================
// Preview
// =========================================================================

@androidx.compose.ui.tooling.preview.Preview(showBackground = true)
@Composable
private fun PreviewPresetScreen() {
    val tc = remember { TcpClient() }
    PresetScreen(tcpClient = tc)
}
