package com.example.typewriterandroid.ui.component

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.tooling.preview.Preview

/**
 * 速度选择器 —— 500 / 1000 / 2000 / 3000。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SpeedSelector(
    value: Int,
    onValueChange: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    val speeds = listOf(500, 1000, 2000, 3000)
    Row(
        modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(4.dp, Alignment.CenterHorizontally)
    ) {
        Text("速度:", modifier = Modifier.align(Alignment.CenterVertically))
        speeds.forEach { speed ->
            FilterChip(
                selected = value == speed,
                onClick = { onValueChange(speed) },
                label = { Text("$speed") }
            )
        }
    }
}

@Preview(showBackground = true)
@Composable
private fun PreviewStepSelector() {
    StepSelector(value = 5.0, onValueChange = {})
}