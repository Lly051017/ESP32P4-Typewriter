package com.example.typewriterandroid.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import androidx.compose.ui.tooling.preview.Preview

/**
 * 五向方向键 —— 上/下/左/右 + HOME。
 */
@Composable
fun JogPad(
    onJog: (axis: String, direction: Int) -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        JogButton("▲", "Y+") { onJog("Y", 1) }
        Row(verticalAlignment = Alignment.CenterVertically) {
            JogButton("◀", "X-")   { onJog("X", -1) }
            JogButton("⌂", "HOME") { onJog("HOME", 0) }
            JogButton("▶", "X+")   { onJog("X", 1) }
        }
        JogButton("▼", "Y-") { onJog("Y", -1) }
    }
}

/** 方向键颜色。 */
private val JogColor = Color(0xFF1976D2)

/**
 * JOG 按钮 —— 支持长按连发。
 *
 * 点击立刻触发一次；按住 200ms 后每 100ms 连发一次。
 * 用 Box + pointerInput 代替 Material3 Button，Button 内部的
 * Surface/clickable 会先消费触摸事件，导致 pointerInput 拿不到按下状态。
 */
@Composable
fun JogButton(
    symbol: String,
    label: String,
    onClick: () -> Unit
) {
    var isPressed by remember { mutableStateOf(false) }

    // 长按连发
    LaunchedEffect(isPressed) {
        if (isPressed) {
            onClick()              // 首击
            delay(200)             // 初始延迟
            while (isPressed) {
                onClick()
                delay(100)         // 连发间隔
            }
        }
    }

    Box(
        modifier = Modifier
            .size(68.dp)
            .padding(3.dp)
            .clip(CircleShape)
            .background(JogColor)
            .pointerInput(Unit) {
                detectTapGestures(
                    onPress = {
                        isPressed = true
                        tryAwaitRelease()
                        isPressed = false
                    }
                )
            },
        contentAlignment = Alignment.Center
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                symbol,
                fontSize = 18.sp,
                color = Color.White,
                textAlign = TextAlign.Center
            )
            Text(
                label,
                fontSize = 9.sp,
                color = Color.White.copy(alpha = 0.8f),
                textAlign = TextAlign.Center
            )
        }
    }
}

@Preview(showBackground = true)
@Composable
private fun PreviewJogPad() {
    JogPad(onJog = { axis, dir -> })
}
