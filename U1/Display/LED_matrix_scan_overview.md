# LED 矩阵扫描梳理图

## 1. 一眼看懂

```text
                         VDD
                          |
        +-----------------+------------------+
        |                 |                  |
      Q1 SS8550         Q2 SS8550          ... Q8 SS8550
      L01 / PB6         L06 / PA1              L15 / PC0
        |                 |                  |
        +-----------------+------------------+
                          |
                    LED 矩阵区
                          |
        +-----------------+------------------+
        |                 |                  |
      L00 / PA0       L02 / PB1            ... L14 / PA5
        |                 |                  |
       1 kΩ              1 kΩ                1 kΩ
        |                 |                  |
       U1 GPIO 低边吸电流，输出 0 点亮
```

高边 Q1-Q8 负责选择一组 LED 公共端，低边 8 路负责吸电流。两边同时有效时，对应 LED 点亮。

## 2. 逻辑矩阵

为了阅读方便，把原理图中的网络重新编号为：

- `H0-H7`：高边选择线，连接 SS8550；
- `L0-L7`：低边吸电流线，连接 U1 和 1 kΩ；
- 实际 LED 只存在于原理图画出的交点，不代表 8×8 的每个位置都有 LED。

```text
                         低边吸电流线
                 L0       L1       L2       L3       L4       L5       L6       L7
              L00/PA0   L02/PB1  L03/PB3  L04/PB2  L05/PB7  L12/PC1  L13/PA6  L14/PA5

高边 H0       LED       LED       LED       LED       LED       LED       LED       LED
L01/PB6

高边 H1       LED       LED       LED       LED       LED       LED       LED       LED
L06/PA1

高边 H2       LED       LED       LED       LED       LED       LED       LED       LED
L07/PA2

高边 H3       LED       LED       LED       LED       LED       LED       LED       LED
L08/PA4

高边 H4       LED       LED       LED       LED       LED       LED       LED       LED
L09/PA3

高边 H5       LED       LED       LED       LED       LED       LED       LED       LED
L10/PB0

高边 H6       LED       LED       LED       LED       LED       LED       LED       LED
L11/PA7

高边 H7       LED       LED       LED       LED       LED       LED       LED       LED
L15/PC0
```

上表是逻辑位置图，实际是否有 LED 要以原理图中画出的 LED 为准。

## 3. 高边选择线

高边为 PNP SS8550，控制逻辑为低有效：

| 扫描线 | 网络 | PY32 引脚 | 输出 0 | 输出 1 |
|---|---|---|---|---|
| H0 | L01，由 Q1 驱动 | PB6 | Q1 导通 | Q1 关闭 |
| H1 | L06，由 Q2 驱动 | PA1 | Q2 导通 | Q2 关闭 |
| H2 | L07，由 Q3 驱动 | PA2 | Q3 导通 | Q3 关闭 |
| H3 | L08，由 Q4 驱动 | PA4 | Q4 导通 | Q4 关闭 |
| H4 | L09，由 Q5 驱动 | PA3 | Q5 导通 | Q5 关闭 |
| H5 | L10，由 Q6 驱动 | PB0 | Q6 导通 | Q6 关闭 |
| H6 | L11，由 Q7 驱动 | PA7 | Q7 导通 | Q7 关闭 |
| H7 | L15，由 Q8 驱动 | PC0 | Q8 导通 | Q8 关闭 |

关闭所有高边时，8 个控制 GPIO 都应保持高电平。

## 4. 低边吸电流线

低边经过 1 kΩ 电阻连接到 U1 GPIO，通常也是低有效：

| 扫描线 | 网络 | PY32 引脚 | 输出 0 | 输出 1 |
|---|---|---|---|---|
| L0 | L00 / P00 | PA0 | 吸电流，允许点亮 | 关闭 |
| L1 | L02 / P02 | PB1 | 吸电流，允许点亮 | 关闭 |
| L2 | L03 / P03 | PB3 | 吸电流，允许点亮 | 关闭 |
| L3 | L04 / P04 | PB2 | 吸电流，允许点亮 | 关闭 |
| L4 | L05 / P05 | PB7 | 吸电流，允许点亮 | 关闭 |
| L5 | L12 / P12 | PC1 | 吸电流，允许点亮 | 关闭 |
| L6 | L13 / P13 | PA6 | 吸电流，允许点亮 | 关闭 |
| L7 | L14 / P14 | PA5 | 吸电流，允许点亮 | 关闭 |

## 5. 单个 LED 的点亮条件

以 `H0-L0` 为例：

```text
P01/PB6 = 0       -> Q1 导通，L01 得到 VDD
P00/PA0 = 0       -> L00 被 U1 通过 1 kΩ 拉低

VDD -> Q1 -> L01 -> LED(H0,L0) -> L00 -> 1 kΩ -> PA0 -> GND
```

只有高边和低边同时有效，LED 才有正向电流。

## 6. 推荐扫描时序

```text
所有高边关闭
        |
设置当前低边数据
        |
打开一路高边
        |
保持 0.5~2 ms
        |
关闭当前高边
        |
切换到下一路高边
        |
重复循环
```

伪代码：

```c
void LED_ScanStep(void)
{
    led_high_all_off();

    /* 设置当前高边对应的低边显示数据 */
    led_low_set(display_buffer[scan_index]);

    /* PNP 高边低电平有效 */
    led_high_select(scan_index);

    scan_index++;
    if (scan_index >= 8U)
    {
        scan_index = 0U;
    }
}
```

建议整个 8 路扫描周期至少大于 100 Hz，实际可使用 500 Hz 左右的刷新频率。切换高边时先全部关闭，可以减少串光和鬼影。

## 7. 按键复用注意事项

S1-S6 使用低边线：

```text
S1 -> L00 / PA0
S2 -> L12 / PC1
S3 -> L05 / PB7
S4 -> L04 / PB2
S5 -> L02 / PB1
S6 -> L03 / PB3
```

读取按键时应暂停 LED：

```text
关闭高边
key_config_Input()
读取 S1-S6
key_config_Output()
恢复 LED 扫描
```

否则低边输出状态会直接影响按键电平。

## 8. 图纸中仍需确认的两点

1. `P15 -> PC0/NRST`：按当前项目方案保留使用，需在 PY32 工程配置中确认 PC0 的 GPIO/NRST 设置。
2. `R30` 左端原理图标注错误：正确连接应为 `L14 -- R30 -- P14`，否则 L7 低边可能悬空。
