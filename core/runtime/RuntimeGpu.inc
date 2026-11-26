    void raise_exception(std::uint32_t code)
    {
        exception_pending_ = true;
        cop0_[14] = in_delay_slot_ ? pc_ - 4 : pc_;
        cop0_[13] = (cop0_[13] & 0xFF00)
            | (code << 2)
            | (in_delay_slot_ ? 0x80000000u : 0);
        cop0_[12] = (cop0_[12] & ~0x3Fu) | ((cop0_[12] << 2) & 0x3Fu);
        pc_ = ((cop0_[12] & 0x00400000u) != 0) ? 0xBFC00180u : 0x80000080u;
    }

    bool execute_gte(std::uint32_t rs, std::uint32_t rt, std::uint32_t rd, std::uint32_t shamt, std::uint32_t instruction)
    {
        if (rs == 0) {
            regs_[rt] = static_cast<std::uint32_t>(gte_data_[rd]);
            return true;
        }
        if (rs == 2) {
            regs_[rt] = gte_control[rd];
            return true;
        }
        if (rs == 4) {
            gte_data_[rd] = static_cast<std::int32_t>(regs_[rt]);
            return true;
        }
        if (rs == 6) {
            gte_control[rd] = regs_[rt];
            return true;
        }
        if (rs == 16 || rs == 20) {
            const std::uint32_t command = instruction & 0x3F;
            const std::int64_t x = gte_data_[9];
            const std::int64_t y = gte_data_[10];
            const std::int64_t z = gte_data_[11];
            const auto sat_signed = [](std::int64_t value, std::int64_t limit, bool lm) {
                const std::int64_t low = lm ? 0 : -32768;
                const std::int64_t high = limit;
                return std::clamp(value, low, high);
            };
            const auto mac_signed = [](std::int64_t value) {
                if (value > 2147483647LL) {
                    return static_cast<std::int64_t>(2147483647LL);
                }
                if (value < -2147483648LL) {
                    return static_cast<std::int64_t>(-2147483648LL);
                }
                return static_cast<std::int64_t>(value);
            };
            const auto matrix_vector = [&](const std::array<std::uint32_t, 32>& matrix,
                const std::array<std::uint32_t, 32>& translation,
                std::int64_t vx, std::int64_t vy, std::int64_t vz,
                bool lm, bool shift) {
                std::array<std::int64_t, 3> result{};
                const std::array<std::array<std::int32_t, 3>, 3> rows{{
                    {{static_cast<std::int16_t>(matrix[0]), static_cast<std::int16_t>(matrix[1]), static_cast<std::int16_t>(matrix[2])}},
                    {{static_cast<std::int16_t>(matrix[3]), static_cast<std::int16_t>(matrix[4]), static_cast<std::int16_t>(matrix[5])}},
                    {{static_cast<std::int16_t>(matrix[6]), static_cast<std::int16_t>(matrix[7]), static_cast<std::int16_t>(matrix[8])}},
                }};
                result[0] = mac_signed((static_cast<std::int64_t>(rows[0][0]) * vx
                    + static_cast<std::int64_t>(rows[0][1]) * vy
                    + static_cast<std::int64_t>(rows[0][2]) * vz
                    + (translation[0] << (shift ? 12 : 0))) >> (shift ? 12 : 0));
                result[1] = mac_signed((static_cast<std::int64_t>(rows[1][0]) * vx
                    + static_cast<std::int64_t>(rows[1][1]) * vy
                    + static_cast<std::int64_t>(rows[1][2]) * vz
                    + (translation[1] << (shift ? 12 : 0))) >> (shift ? 12 : 0));
                result[2] = mac_signed((static_cast<std::int64_t>(rows[2][0]) * vx
                    + static_cast<std::int64_t>(rows[2][1]) * vy
                    + static_cast<std::int64_t>(rows[2][2]) * vz
                    + (translation[2] << (shift ? 12 : 0))) >> (shift ? 12 : 0));
                gte_control[24] = static_cast<std::uint32_t>(result[0]);
                gte_control[25] = static_cast<std::uint32_t>(result[1]);
                gte_control[26] = static_cast<std::uint32_t>(result[2]);
                gte_data_[9] = static_cast<std::int32_t>(sat_signed(result[0], lm ? 32767 : 32767, lm));
                gte_data_[10] = static_cast<std::int32_t>(sat_signed(result[1], lm ? 32767 : 32767, lm));
                gte_data_[11] = static_cast<std::int32_t>(sat_signed(result[2], lm ? 32767 : 32767, lm));
                return result;
            };
            const bool lm = (instruction & (1u << 10)) != 0;
            const bool sf = (instruction & (1u << 19)) != 0;
            gte_control[31] = 0;

            auto fixed12 = [](std::int64_t value, bool shift) {
                if (!shift) return value;
                return value >= 0 ? (value + 2048) / 4096 : (value - 2048) / 4096;
            };
            auto clamp_mac = [&](std::int64_t value, std::uint32_t overflow_flag) {
                if (value < -2147483648LL || value > 2147483647LL) {
                    gte_control[31] |= overflow_flag;
                }
                return std::clamp<std::int64_t>(value, -2147483648LL, 2147483647LL);
            };
            auto clamp_ir = [&](std::int64_t value, bool lm_mode, std::uint32_t overflow_flag) {
                const std::int64_t low = lm_mode ? 0 : -32768;
                if (value < low || value > 32767) gte_control[31] |= overflow_flag;
                return std::clamp<std::int64_t>(value, low, 32767LL);
            };
            auto matrix = [&]() {
                return std::array<std::int16_t, 9>{{
                    static_cast<std::int16_t>(gte_control[0]), static_cast<std::int16_t>(gte_control[0] >> 16),
                    static_cast<std::int16_t>(gte_control[1]), static_cast<std::int16_t>(gte_control[1] >> 16),
                    static_cast<std::int16_t>(gte_control[2]), static_cast<std::int16_t>(gte_control[2] >> 16),
                    static_cast<std::int16_t>(gte_control[3]), static_cast<std::int16_t>(gte_control[3] >> 16),
                    static_cast<std::int16_t>(gte_control[4])
                }};
            };
            auto transform = [&](std::int64_t vx, std::int64_t vy, std::int64_t vz,
                bool translation, bool shift) {
                const auto r = matrix();
                std::array<std::int64_t, 3> mac{
                    translation ? static_cast<std::int32_t>(gte_control[5]) : 0,
                    translation ? static_cast<std::int32_t>(gte_control[6]) : 0,
                    translation ? static_cast<std::int32_t>(gte_control[7]) : 0
                };
                mac[0] += r[0] * vx + r[1] * vy + r[2] * vz;
                mac[1] += r[3] * vx + r[4] * vy + r[5] * vz;
                mac[2] += r[6] * vx + r[7] * vy + r[8] * vz;
                mac[0] = clamp_mac(mac[0], 0x80000000u);
                mac[1] = clamp_mac(mac[1], 0x40000000u);
                mac[2] = clamp_mac(mac[2], 0x20000000u);
                gte_control[25] = static_cast<std::uint32_t>(mac[0]);
                gte_control[26] = static_cast<std::uint32_t>(mac[1]);
                gte_control[27] = static_cast<std::uint32_t>(mac[2]);
                std::array<std::int64_t, 3> ir{
                    fixed12(mac[0], shift), fixed12(mac[1], shift), fixed12(mac[2], shift)
                };
                ir[0] = clamp_ir(ir[0], lm, 0x08000000u);
                ir[1] = clamp_ir(ir[1], lm, 0x04000000u);
                ir[2] = clamp_ir(ir[2], lm, 0x02000000u);
                gte_control[9] = static_cast<std::uint32_t>(ir[0]);
                gte_control[10] = static_cast<std::uint32_t>(ir[1]);
                gte_control[11] = static_cast<std::uint32_t>(ir[2]);
                gte_data_[9] = static_cast<std::int32_t>(ir[0]);
                gte_data_[10] = static_cast<std::int32_t>(ir[1]);
                gte_data_[11] = static_cast<std::int32_t>(ir[2]);
                return ir;
            };
            std::uint32_t cycles = 1;
            switch (command) {
            case 0x01:
            case 0x30:
            {
                const std::array<std::int64_t, 3> ir = transform(gte_data_[9], gte_data_[10], gte_data_[11], true, sf);
                gte_data_[19] = static_cast<std::int32_t>(std::clamp<std::int64_t>(z, 0, 65535));
                gte_control[26] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(z, 0, 65535));
                gte_control[24] = static_cast<std::uint32_t>(ir[0]);
                gte_control[25] = static_cast<std::uint32_t>(ir[1]);
                cycles = command == 0x30 ? 23 : 15;
                break;
            }
            case 0x06:
            {
                const std::int64_t value = gte_data_[9] * gte_data_[10]
                    - gte_data_[10] * gte_data_[11]
                    + gte_data_[11] * gte_data_[12];
                gte_control[24] = static_cast<std::uint32_t>(value);
                if (value < 0) gte_control[31] |= 0x00004000u;
                cycles = 8;
                break;
            }
            case 0x0C:
            {
                const std::int64_t ax = gte_data_[9];
                const std::int64_t ay = gte_data_[10];
                const std::int64_t az = gte_data_[11];
                const std::int64_t bx = gte_data_[12];
                const std::int64_t by = gte_data_[13];
                const std::int64_t bz = gte_data_[14];
                const std::array<std::int64_t, 3> mac{
                    ay * bz - az * by,
                    az * bx - ax * bz,
                    ax * by - ay * bx
                };
                gte_control[25] = static_cast<std::uint32_t>(clamp_mac(mac[0], 0x80000000u));
                gte_control[26] = static_cast<std::uint32_t>(clamp_mac(mac[1], 0x40000000u));
                gte_control[27] = static_cast<std::uint32_t>(clamp_mac(mac[2], 0x20000000u));
                gte_control[9] = static_cast<std::uint32_t>(clamp_ir(fixed12(mac[0], sf), lm, 0x08000000u));
                gte_control[10] = static_cast<std::uint32_t>(clamp_ir(fixed12(mac[1], sf), lm, 0x04000000u));
                gte_control[11] = static_cast<std::uint32_t>(clamp_ir(fixed12(mac[2], sf), lm, 0x02000000u));
                gte_control[24] = gte_control[9];
                cycles = 6;
                break;
            }
            case 0x12:
            {
                std::int64_t vx = gte_data_[9], vy = gte_data_[10], vz = gte_data_[11];
                const std::uint32_t vector = (instruction >> 15) & 3u;
                if (vector == 1) { vx = gte_data_[0]; vy = gte_data_[1]; vz = gte_data_[2]; }
                if (vector == 2) { vx = gte_data_[2]; vy = gte_data_[3]; vz = gte_data_[4]; }
                if (vector == 3) { vx = gte_data_[4]; vy = gte_data_[5]; vz = gte_data_[6]; }
                transform(vx, vy, vz, ((instruction >> 19) & 1) == 0, sf);
                cycles = 8;
                break;
            }
            case 0x14:
            {
                auto square = [&](std::int64_t value) {
                    const std::int64_t result = fixed12(value * value, true);
                    return clamp_ir(result, lm, 0x02000000u);
                };
                gte_control[9] = static_cast<std::uint32_t>(square(x));
                gte_control[10] = static_cast<std::uint32_t>(square(y));
                gte_control[11] = static_cast<std::uint32_t>(square(z));
                gte_control[24] = gte_control[9];
                gte_control[25] = gte_control[10];
                gte_control[26] = gte_control[11];
                gte_data_[9] = static_cast<std::int32_t>(gte_control[9]);
                gte_data_[10] = static_cast<std::int32_t>(gte_control[10]);
                gte_data_[11] = static_cast<std::int32_t>(gte_control[11]);
                cycles = command == 0x14 ? 5 : (command == 0x28 ? 17 : 8);
                break;
            }
            case 0x28:
            {
                const std::int64_t ir1 = static_cast<std::int64_t>(gte_control[9]);
                const std::int64_t ir2 = static_cast<std::int64_t>(gte_control[10]);
                const std::int64_t ir3 = static_cast<std::int64_t>(gte_control[11]);
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(ir1, 0, 32767));
                gte_control[25] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(ir2, 0, 32767));
                gte_control[26] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(ir3, 0, 32767));
                gte_data_[6] = static_cast<std::int32_t>(
                    (std::clamp<std::int64_t>(ir1, 0, 255))
                    | (std::clamp<std::int64_t>(ir2, 0, 255) << 8)
                    | (std::clamp<std::int64_t>(ir3, 0, 255) << 16));
                cycles = 17;
                break;
            }
            case 0x29:
            {
                const std::int64_t code = gte_control[8];
                auto dcpl_channel = [&](std::int64_t base, std::int64_t ir) {
                    const std::int64_t value = base * gte_control[29] / 4096
                        + ir * code / 4096;
                    gte_control[31] |= value < 0 || value > 32767 ? 0x10000000u : 0;
                    return std::clamp<std::int64_t>(value, 0, 32767);
                };
                const std::int64_t r = dcpl_channel(gte_data_[6] & 255, gte_control[9]);
                const std::int64_t g = dcpl_channel((gte_data_[6] >> 8) & 255, gte_control[10]);
                const std::int64_t b = dcpl_channel((gte_data_[6] >> 16) & 255, gte_control[11]);
                gte_control[24] = static_cast<std::uint32_t>(r);
                gte_control[25] = static_cast<std::uint32_t>(g);
                gte_control[26] = static_cast<std::uint32_t>(b);
                gte_control[9] = static_cast<std::uint32_t>(r);
                gte_control[10] = static_cast<std::uint32_t>(g);
                gte_control[11] = static_cast<std::uint32_t>(b);
                cycles = 8;
                break;
            }
            case 0x2A:
            {
                const std::int64_t zsf = gte_control[29] == 0 ? 4096 : gte_control[29];
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    (gte_data_[12] + gte_data_[13] + gte_data_[14]) * zsf / (3 * 4096),
                    0LL, 65535LL));
                cycles = 5;
                break;
            }
            case 0x2D:
            {
                const std::int64_t zsf = gte_control[30] == 0 ? 4096 : gte_control[30];
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    (gte_data_[16] + gte_data_[17] + gte_data_[18] + gte_data_[19]) * zsf / (4 * 4096),
                    0LL, 65535LL));
                cycles = 6;
                break;
            }
            case 0x2E:
            {
                const std::int64_t zsf = gte_control[29] == 0 ? 4096 : gte_control[29];
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    (gte_data_[17] + gte_data_[18] + gte_data_[19]) * zsf / (3 * 4096),
                    0LL, 65535LL));
                cycles = 5;
                break;
            }
            case 0x10:
            case 0x11:
            case 0x13:
            case 0x16:
            case 0x1B:
            case 0x1C:
            case 0x1E:
            case 0x1F:
            case 0x3D:
            case 0x3E:
            {
                const std::int64_t ir1 = gte_control[9] & 0xFFFF;
                const std::int64_t ir2 = gte_control[10] & 0xFFFF;
                const std::int64_t ir3 = gte_control[11] & 0xFFFF;
                const std::int64_t r = gte_data_[6] & 255;
                const std::int64_t g = (gte_data_[6] >> 8) & 255;
                const std::int64_t b = (gte_data_[6] >> 16) & 255;
                auto color_mac = [&](std::int64_t base, std::int64_t light) {
                    const std::int64_t value = base + fixed12(light * gte_control[8], sf);
                    gte_control[31] |= value < -2147483648LL || value > 2147483647LL ? 0x20000000u : 0;
                    return std::clamp<std::int64_t>(value, 0, 32767);
                };
                const std::int64_t macr = command == 0x3D
                    ? fixed12(gte_control[29] * gte_control[8], sf)
                    : color_mac(r, ir1);
                const std::int64_t macg = command == 0x3D
                    ? fixed12(gte_control[29] * gte_control[8], sf)
                    : color_mac(g, ir2);
                const std::int64_t macb = command == 0x3D
                    ? fixed12(gte_control[29] * gte_control[8], sf)
                    : color_mac(b, ir3);
                gte_control[25] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(macr, 0, 32767));
                gte_control[26] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(macg, 0, 32767));
                gte_control[27] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(macb, 0, 32767));
                gte_control[9] = static_cast<std::uint32_t>(clamp_ir(macr, lm, 0x08000000u));
                gte_control[10] = static_cast<std::uint32_t>(clamp_ir(macg, lm, 0x04000000u));
                gte_control[11] = static_cast<std::uint32_t>(clamp_ir(macb, lm, 0x02000000u));
                gte_data_[6] = static_cast<std::int32_t>(
                    (std::clamp<std::int64_t>(macr, 0, 255))
                    | (std::clamp<std::int64_t>(macg, 0, 255) << 8)
                    | (std::clamp<std::int64_t>(macb, 0, 255) << 16));
                cycles = command == 0x10 ? 8 : command == 0x11 ? 8 : command == 0x13 ? 19 :
                    command == 0x16 ? 44 : command == 0x1B ? 17 : command == 0x1C ? 39 :
                    command == 0x1E ? 14 : command == 0x1F ? 11 : 5;
                break;
            }
            default:
                unsupported_ = "unsupported COP2 GTE command";
                return false;
            }
            gte_cycle_count_ += cycles;
            return true;
        }
        unsupported_ = "unsupported COP2 instruction";
        return false;
    }

    std::uint16_t gpu_color(std::uint32_t word) const
    {
        const std::uint32_t r = word & 0xFF;
        const std::uint32_t g = (word >> 8) & 0xFF;
        const std::uint32_t b = (word >> 16) & 0xFF;
        return static_cast<std::uint16_t>(
            ((r >> 3) & 0x1F)
            | (((g >> 3) & 0x1F) << 5)
            | (((b >> 3) & 0x1F) << 10)
        );
    }

    void put_pixel(std::uint32_t x, std::uint32_t y, std::uint16_t color, bool semi_transparent = false)
    {
        x = (gpu_draw_offset_x & 0x8000) ? x - (0x10000u - gpu_draw_offset_x) : x + gpu_draw_offset_x;
        y = (gpu_draw_offset_y & 0x8000) ? y - (0x10000u - gpu_draw_offset_y) : y + gpu_draw_offset_y;
        x &= 1023u;
        y &= 511u;
        if (x >= gpu_draw_x1 && x <= gpu_draw_x2 && y >= gpu_draw_y1 && y <= gpu_draw_y2
            && x < 1024 && y < 512) {
            if (gpu_mask_keep && (vram_[y * 1024 + x] & 0x8000u) != 0) {
                return;
            }
            if (semi_transparent) {
                const std::uint16_t back = vram_[y * 1024 + x];
                const std::uint32_t br = back & 31u;
                const std::uint32_t bg = (back >> 5) & 31u;
                const std::uint32_t bb = (back >> 10) & 31u;
                const std::uint32_t fr = color & 31u;
                const std::uint32_t fg = (color >> 5) & 31u;
                const std::uint32_t fb = (color >> 10) & 31u;
                const std::uint32_t mode = (gpu_status >> 5) & 3u;
                auto blend_channel = [&](std::uint32_t b, std::uint32_t f) {
                    std::uint32_t result;
                    switch (mode) {
                    case 0: result = (b + f) >> 1; break;
                    case 1: result = b + f; break;
                    case 2: result = b >= f ? b - f : 0; break;
                    default: result = b + (f >> 2); break;
                    }
                    return std::min(result, 31u);
                };
                color = static_cast<std::uint16_t>(
                    blend_channel(br, fr)
                    | (blend_channel(bg, fg) << 5)
                    | (blend_channel(bb, fb) << 10));
            }
            if (gpu_mask_set) {
                color |= 0x8000u;
            }
            vram_[y * 1024 + x] = color;
            ++gpu_vram_write_count;
        }
    }

    void fill_rectangle(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::uint16_t color)
    {
        for (std::uint32_t row = y; row < y + height && row < 512; ++row) {
            for (std::uint32_t column = x; column < x + width && column < 1024; ++column) {
                put_pixel(column, row, color);
            }
        }
    }

    void draw_flat_triangle(
        std::uint32_t x1, std::uint32_t y1,
        std::uint32_t x2, std::uint32_t y2,
        std::uint32_t x3, std::uint32_t y3,
        std::uint16_t color,
        bool semi_transparent = false
    )
    {
        const std::int64_t area =
            (static_cast<std::int64_t>(x2) - x1) * (static_cast<std::int64_t>(y3) - y1)
            - (static_cast<std::int64_t>(y2) - y1) * (static_cast<std::int64_t>(x3) - x1);
        if (area == 0) {
            return;
        }

        std::uint32_t min_x = std::min({x1, x2, x3});
        std::uint32_t min_y = std::min({y1, y2, y3});
        std::uint32_t max_x = std::max({x1, x2, x3});
        std::uint32_t max_y = std::max({y1, y2, y3});
        for (std::uint32_t y = min_y; y <= max_y && y < 512; ++y) {
            for (std::uint32_t x = min_x; x <= max_x && x < 1024; ++x) {
                const std::int64_t edge1 =
                    (static_cast<std::int64_t>(x2) - x1) * (static_cast<std::int64_t>(y) - y1)
                    - (static_cast<std::int64_t>(y2) - y1) * (static_cast<std::int64_t>(x) - x1);
                const std::int64_t edge2 =
                    (static_cast<std::int64_t>(x3) - x2) * (static_cast<std::int64_t>(y) - y2)
                    - (static_cast<std::int64_t>(y3) - y2) * (static_cast<std::int64_t>(x) - x2);
                const std::int64_t edge3 =
                    (static_cast<std::int64_t>(x1) - x3) * (static_cast<std::int64_t>(y) - y3)
                    - (static_cast<std::int64_t>(y1) - y3) * (static_cast<std::int64_t>(x) - x3);
                if ((area > 0 && edge1 >= 0 && edge2 >= 0 && edge3 >= 0)
                    || (area < 0 && edge1 <= 0 && edge2 <= 0 && edge3 <= 0)) {
                    put_pixel(x, y, color, semi_transparent);
                }
            }
        }
    }

        std::uint16_t texture_pixel(std::uint32_t clut, std::uint32_t u, std::uint32_t v) const
        {
            const std::uint32_t texture_mode = (gpu_status >> 7) & 3u;
            const std::uint32_t page_x = (gpu_status & 0x0Fu) * 64u;
            const std::uint32_t page_y = ((gpu_status >> 4) & 1u) * 256u;
            const std::uint32_t tx = page_x + (u & 0xFFu);
            const std::uint32_t ty = page_y + (v & 0xFFu);
            if (tx >= 1024 || ty >= 512) {
                return 0;
            }

            if (texture_mode == 0) {
                const std::uint32_t pixel = vram_[ty * 1024 + (tx >> 2)];
                const std::uint32_t index = (pixel >> ((tx & 3u) * 4u)) & 0x0Fu;
                const std::uint32_t clut_x = (clut & 0x3Fu) * 16u;
                const std::uint32_t clut_y = (clut >> 6) & 0x1FFu;
                const std::uint32_t nibble = (clut_x + index) & 1023u;
                if (clut_y >= 512) {
                    return 0;
                }
                return vram_[clut_y * 1024 + nibble];
            }
            if (texture_mode == 1) {
                const std::uint32_t pixel = vram_[ty * 1024 + (tx >> 1)];
                const std::uint32_t index = (pixel >> ((tx & 1u) * 8u)) & 0xFFu;
                const std::uint32_t clut_x = (clut & 0x3Fu) * 16u;
                const std::uint32_t clut_y = (clut >> 6) & 0x1FFu;
                if (clut_y >= 512) {
                    return 0;
                }
                return vram_[clut_y * 1024 + clut_x + index];
            }
            return vram_[ty * 1024 + tx];
        }

        void draw_textured_triangle(
            std::uint32_t x1, std::uint32_t y1, std::uint32_t u1, std::uint32_t v1,
            std::uint32_t x2, std::uint32_t y2, std::uint32_t u2, std::uint32_t v2,
            std::uint32_t x3, std::uint32_t y3, std::uint32_t u3, std::uint32_t v3,
            std::uint32_t clut, bool semi_transparent = false
        )
        {
            const std::int64_t area =
                (static_cast<std::int64_t>(x2) - x1) * (static_cast<std::int64_t>(y3) - y1)
                - (static_cast<std::int64_t>(y2) - y1) * (static_cast<std::int64_t>(x3) - x1);
            if (area == 0) {
                return;
            }

            const std::uint32_t min_x = std::min({x1, x2, x3});
            const std::uint32_t min_y = std::min({y1, y2, y3});
            const std::uint32_t max_x = std::max({x1, x2, x3});
            const std::uint32_t max_y = std::max({y1, y2, y3});
            for (std::uint32_t y = min_y; y <= max_y && y < 512; ++y) {
                for (std::uint32_t x = min_x; x <= max_x && x < 1024; ++x) {
                    const std::int64_t e1 =
                        (static_cast<std::int64_t>(x2) - x1) * (static_cast<std::int64_t>(y) - y1)
                        - (static_cast<std::int64_t>(y2) - y1) * (static_cast<std::int64_t>(x) - x1);
                    const std::int64_t e2 =
                        (static_cast<std::int64_t>(x3) - x2) * (static_cast<std::int64_t>(y) - y2)
                        - (static_cast<std::int64_t>(y3) - y2) * (static_cast<std::int64_t>(x) - x2);
                    const std::int64_t e3 =
                        (static_cast<std::int64_t>(x1) - x3) * (static_cast<std::int64_t>(y) - y3)
                        - (static_cast<std::int64_t>(y1) - y3) * (static_cast<std::int64_t>(x) - x3);
                    if ((area > 0 && e1 >= 0 && e2 >= 0 && e3 >= 0)
                        || (area < 0 && e1 <= 0 && e2 <= 0 && e3 <= 0)) {
                        const std::int64_t w1 = area > 0
                            ? e3 : -e3;
                        const std::int64_t w2 = area > 0
                            ? e1 : -e1;
                        const std::int64_t w3 = area > 0
                            ? e2 : -e2;
                        const std::int64_t denom = w1 + w2 + w3;
                        if (denom == 0) {
                            continue;
                        }
                        const std::uint32_t u = static_cast<std::uint32_t>(
                            std::clamp<std::int64_t>((u1 * w1 + u2 * w2 + u3 * w3) / denom, 0, 255));
                        const std::uint32_t v = static_cast<std::uint32_t>(
                            std::clamp<std::int64_t>((v1 * w1 + v2 * w2 + v3 * w3) / denom, 0, 255));
                        const std::uint16_t texel = texture_pixel(clut, u, v);
                        if (texel != 0 || ((gp0_command >> 24) & 0x02) == 0) {
                            put_pixel(x, y, texel, semi_transparent);
                        }
                    }
                }
            }
        }

    void draw_gouraud_triangle(
        std::uint32_t x1, std::uint32_t y1, std::uint32_t c1,
        std::uint32_t x2, std::uint32_t y2, std::uint32_t c2,
        std::uint32_t x3, std::uint32_t y3, std::uint32_t c3,
        bool semi_transparent
    )
    {
        const std::int64_t area =
            (static_cast<std::int64_t>(x2) - x1) * (static_cast<std::int64_t>(y3) - y1)
            - (static_cast<std::int64_t>(y2) - y1) * (static_cast<std::int64_t>(x3) - x1);
        if (area == 0) return;
        const std::uint32_t min_x = std::min({x1, x2, x3});
        const std::uint32_t min_y = std::min({y1, y2, y3});
        const std::uint32_t max_x = std::max({x1, x2, x3});
        const std::uint32_t max_y = std::max({y1, y2, y3});
        for (std::uint32_t y = min_y; y <= max_y && y < 512; ++y) {
            for (std::uint32_t x = min_x; x <= max_x && x < 1024; ++x) {
                const std::int64_t e1 =
                    (static_cast<std::int64_t>(x2) - x1) * (static_cast<std::int64_t>(y) - y1)
                    - (static_cast<std::int64_t>(y2) - y1) * (static_cast<std::int64_t>(x) - x1);
                const std::int64_t e2 =
                    (static_cast<std::int64_t>(x3) - x2) * (static_cast<std::int64_t>(y) - y2)
                    - (static_cast<std::int64_t>(y3) - y2) * (static_cast<std::int64_t>(x) - x2);
                const std::int64_t e3 =
                    (static_cast<std::int64_t>(x1) - x3) * (static_cast<std::int64_t>(y) - y3)
                    - (static_cast<std::int64_t>(y1) - y3) * (static_cast<std::int64_t>(x) - x3);
                if ((area > 0 && e1 >= 0 && e2 >= 0 && e3 >= 0)
                    || (area < 0 && e1 <= 0 && e2 <= 0 && e3 <= 0)) {
                    const std::int64_t w1 = area > 0 ? e3 : -e3;
                    const std::int64_t w2 = area > 0 ? e1 : -e1;
                    const std::int64_t w3 = area > 0 ? e2 : -e2;
                    const std::int64_t denom = w1 + w2 + w3;
                    if (denom == 0) continue;
                    auto channel = [&](std::uint32_t shift) {
                        const std::int64_t v1 = (c1 >> shift) & 255u;
                        const std::int64_t v2 = (c2 >> shift) & 255u;
                        const std::int64_t v3 = (c3 >> shift) & 255u;
                        return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                            (v1 * w1 + v2 * w2 + v3 * w3) / denom, 0, 255));
                    };
                    const std::uint32_t r = channel(0);
                    const std::uint32_t g = channel(8);
                    const std::uint32_t b = channel(16);
                    put_pixel(x, y, static_cast<std::uint16_t>(
                        ((r >> 3) & 31u) | (((g >> 3) & 31u) << 5) | (((b >> 3) & 31u) << 10)),
                        semi_transparent);
                }
            }
        }
    }

    void draw_gouraud_textured_triangle(
        std::uint32_t x1, std::uint32_t y1, std::uint32_t u1, std::uint32_t v1, std::uint32_t c1,
        std::uint32_t x2, std::uint32_t y2, std::uint32_t u2, std::uint32_t v2, std::uint32_t c2,
        std::uint32_t x3, std::uint32_t y3, std::uint32_t u3, std::uint32_t v3, std::uint32_t c3,
        std::uint32_t clut, bool semi_transparent
    )
    {
        const std::int64_t area =
            (static_cast<std::int64_t>(x2) - x1) * (static_cast<std::int64_t>(y3) - y1)
            - (static_cast<std::int64_t>(y2) - y1) * (static_cast<std::int64_t>(x3) - x1);
        if (area == 0) return;
        const std::uint32_t min_x = std::min({x1, x2, x3});
        const std::uint32_t min_y = std::min({y1, y2, y3});
        const std::uint32_t max_x = std::max({x1, x2, x3});
        const std::uint32_t max_y = std::max({y1, y2, y3});
        for (std::uint32_t y = min_y; y <= max_y && y < 512; ++y) {
            for (std::uint32_t x = min_x; x <= max_x && x < 1024; ++x) {
                const std::int64_t e1 =
                    (static_cast<std::int64_t>(x2) - x1) * (static_cast<std::int64_t>(y) - y1)
                    - (static_cast<std::int64_t>(y2) - y1) * (static_cast<std::int64_t>(x) - x1);
                const std::int64_t e2 =
                    (static_cast<std::int64_t>(x3) - x2) * (static_cast<std::int64_t>(y) - y2)
                    - (static_cast<std::int64_t>(y3) - y2) * (static_cast<std::int64_t>(x) - x2);
                const std::int64_t e3 =
                    (static_cast<std::int64_t>(x1) - x3) * (static_cast<std::int64_t>(y) - y3)
                    - (static_cast<std::int64_t>(y1) - y3) * (static_cast<std::int64_t>(x) - x3);
                if ((area > 0 && e1 >= 0 && e2 >= 0 && e3 >= 0)
                    || (area < 0 && e1 <= 0 && e2 <= 0 && e3 <= 0)) {
                    const std::int64_t w1 = area > 0 ? e3 : -e3;
                    const std::int64_t w2 = area > 0 ? e1 : -e1;
                    const std::int64_t w3 = area > 0 ? e2 : -e2;
                    const std::int64_t denom = w1 + w2 + w3;
                    if (denom == 0) continue;
                    const std::uint32_t u = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                        (u1 * w1 + u2 * w2 + u3 * w3) / denom, 0, 255));
                    const std::uint32_t v = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                        (v1 * w1 + v2 * w2 + v3 * w3) / denom, 0, 255));
                    std::uint16_t texel = texture_pixel(clut, u, v);
                    auto shade = [&](std::uint32_t shift) {
                        const std::int64_t a = (c1 >> shift) & 255u;
                        const std::int64_t b = (c2 >> shift) & 255u;
                        const std::int64_t c = (c3 >> shift) & 255u;
                        return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                            (a * w1 + b * w2 + c * w3) / denom, 0, 255));
                    };
                    const std::uint32_t tr = texel & 31u;
                    const std::uint32_t tg = (texel >> 5) & 31u;
                    const std::uint32_t tb = (texel >> 10) & 31u;
                    const std::uint32_t r = shade(0) * tr / 32u;
                    const std::uint32_t g = shade(8) * tg / 32u;
                    const std::uint32_t b = shade(16) * tb / 32u;
                    texel = static_cast<std::uint16_t>(r | (g << 5) | (b << 10));
                    if (texel != 0 || !semi_transparent) {
                        put_pixel(x, y, texel, semi_transparent);
                    }
                }
            }
        }
    }

    void gp0_store(std::uint32_t word)
    {
        ++gpu_write_count;
        if (std::getenv("ANYPS1_GPU_TRACE") != nullptr) {
            std::fprintf(stderr, "GP0=0x%08X writes=%llu vrampix=%llu pc=0x%08X\n",
                static_cast<unsigned>(word),
                static_cast<unsigned long long>(gpu_write_count),
                static_cast<unsigned long long>(gpu_vram_write_count),
                static_cast<unsigned>(pc_));
        }
        if (gp0_transfer_remaining != 0) {
            for (std::uint32_t pixel = 0; pixel < 2 && gp0_transfer_remaining != 0; ++pixel) {
                std::uint16_t value = static_cast<std::uint16_t>(
                    pixel == 0 ? word & 0xFFFF : word >> 16
                );
                gp0_transfer_x = (gp0_transfer_start_x + gp0_transfer_cursor % gp0_transfer_width) & 1023u;
                gp0_transfer_y = (gp0_transfer_start_y + gp0_transfer_cursor / gp0_transfer_width) & 511u;
                if (gp0_transfer_y < 512 && gp0_transfer_x < 1024) {
                    std::uint16_t* destination = &vram_[gp0_transfer_y * 1024 + gp0_transfer_x];
                    if (!gpu_mask_keep || (*destination & 0x8000u) == 0) {
                        if (gpu_mask_set) value |= 0x8000u;
                        *destination = value;
                        ++gpu_vram_write_count;
                    }
                }
                ++gp0_transfer_cursor;
                --gp0_transfer_remaining;
            }
            if (gp0_transfer_remaining == 0) {
                gpu_status |= 0x04000000;
            }
            return;
        }

        if (gp0_expected == 0) {
            gp0_command = word;
            gp0_param_count = 0;
            const std::uint32_t opcode = word >> 24;
            if (opcode == 0x01) {
                gp0_expected = 0;
            } else if (opcode == 0x02) {
                gp0_expected = 3;
            } else if (opcode >= 0x20 && opcode <= 0x3F) {
                const bool textured = (opcode & 0x04) != 0;
                const bool gouraud = (opcode & 0x10) != 0;
                const bool quad = (opcode & 0x08) != 0;
                gp0_expected = 0;
                if (!textured && !gouraud) {
                    gp0_expected = quad ? 4 : 3;
                } else if (textured && !gouraud) {
                    gp0_expected = quad ? 8 : 6;
                } else if (!textured && gouraud) {
                    gp0_expected = quad ? 8 : 6;
                } else {
                    gp0_expected = quad ? 12 : 9;
                }
            } else if ((opcode >= 0x60 && opcode <= 0x67) || (opcode >= 0x68 && opcode <= 0x7F)) {
                gp0_expected = (opcode & 0x04) != 0 ? 3 : 2;
            } else if (opcode >= 0x40 && opcode <= 0x5F) {
                gp0_expected = 2;
            } else if (opcode == 0xA0) {
                gp0_expected = 2;
            } else if (opcode == 0xC0) {
                gp0_expected = 2;
            } else if (opcode == 0x80) {
                gp0_expected = 3;
            } else if (opcode == 0xE1) {
                gp0_expected = 1;
            } else if (opcode == 0xE2) {
                gp0_expected = 1;
            } else if (opcode == 0xE3 || opcode == 0xE4 || opcode == 0xE5 || opcode == 0xE6) {
                gp0_expected = 1;
            } else {
                gp0_expected = 0;
            }
            if (gp0_expected == 0) {
                gp0_command = 0;
            }
            return;
        }

        gp0_params_[gp0_param_count++] = word;
        if (gp0_param_count != gp0_expected) {
            return;
        }

        const std::uint32_t opcode = gp0_command >> 24;
        if (opcode == 0x01) {
            gp0_param_count = 0;
            gp0_expected = 0;
            gp0_command = 0;
            gp0_transfer_remaining = 0;
            gpu_status = 0x1F400000;
        } else if (opcode == 0x02) {
            const std::uint32_t x = gp0_params_[1] & 0xFFFF;
            const std::uint32_t y = gp0_params_[1] >> 16;
            const std::uint32_t width = gp0_params_[2] & 0xFFFF;
            const std::uint32_t height = gp0_params_[2] >> 16;
            fill_rectangle(x, y, width, height, gpu_color(gp0_params_[0]));
        } else if (opcode >= 0x20 && opcode <= 0x3F) {
            const bool quad = (opcode & 0x08) != 0;
            const bool gouraud = (opcode & 0x10) != 0;
            const bool textured = (opcode & 0x04) != 0;
            if (!gouraud) {
                if (textured) {
                    const std::uint32_t u1 = gp0_params_[0] & 0xFF;
                    const std::uint32_t v1 = (gp0_params_[0] >> 8) & 0xFF;
                    const std::uint32_t clut = gp0_params_[0] >> 16;
                    const std::uint32_t x1 = gp0_params_[1] & 0xFFFF;
                    const std::uint32_t y1 = gp0_params_[1] >> 16;
                    const std::uint32_t u2 = gp0_params_[2] & 0xFF;
                    const std::uint32_t v2 = (gp0_params_[2] >> 8) & 0xFF;
                    const std::uint32_t x2 = gp0_params_[3] & 0xFFFF;
                    const std::uint32_t y2 = gp0_params_[3] >> 16;
                    const std::uint32_t u3 = gp0_params_[4] & 0xFF;
                    const std::uint32_t v3 = (gp0_params_[4] >> 8) & 0xFF;
                    const std::uint32_t x3 = gp0_params_[5] & 0xFFFF;
                    const std::uint32_t y3 = gp0_params_[5] >> 16;
                    draw_textured_triangle(x1, y1, u1, v1, x2, y2, u2, v2, x3, y3, u3, v3, clut,
                        (opcode & 0x02) != 0);
                    if (quad) {
                        const std::uint32_t u4 = gp0_params_[6] & 0xFF;
                        const std::uint32_t v4 = (gp0_params_[6] >> 8) & 0xFF;
                        const std::uint32_t x4 = gp0_params_[7] & 0xFFFF;
                        const std::uint32_t y4 = gp0_params_[7] >> 16;
                        draw_textured_triangle(x1, y1, u1, v1, x3, y3, u3, v3, x4, y4, u4, v4, clut,
                            (opcode & 0x02) != 0);
                    }
                } else {
                    const std::uint32_t x1 = gp0_params_[0] & 0xFFFF;
                    const std::uint32_t y1 = gp0_params_[0] >> 16;
                    const std::uint32_t x2 = gp0_params_[1] & 0xFFFF;
                    const std::uint32_t y2 = gp0_params_[1] >> 16;
                    const std::uint32_t x3 = gp0_params_[2] & 0xFFFF;
                    const std::uint32_t y3 = gp0_params_[2] >> 16;
                    const std::uint16_t color = gpu_color(gp0_command);
                    draw_flat_triangle(x1, y1, x2, y2, x3, y3, color, (opcode & 0x02) != 0);
                    if (quad) {
                        const std::uint32_t x4 = gp0_params_[3] & 0xFFFF;
                        const std::uint32_t y4 = gp0_params_[3] >> 16;
                        draw_flat_triangle(x1, y1, x3, y3, x4, y4, color, (opcode & 0x02) != 0);
                    }
                }
            } else if (textured) {
                const std::uint32_t x1 = gp0_params_[2] & 0xFFFF;
                const std::uint32_t y1 = gp0_params_[2] >> 16;
                const std::uint32_t x2 = gp0_params_[5] & 0xFFFF;
                const std::uint32_t y2 = gp0_params_[5] >> 16;
                const std::uint32_t x3 = gp0_params_[8] & 0xFFFF;
                const std::uint32_t y3 = gp0_params_[8] >> 16;
                const std::uint32_t u1 = gp0_params_[1] & 0xFF;
                const std::uint32_t v1 = (gp0_params_[1] >> 8) & 0xFF;
                const std::uint32_t u2 = gp0_params_[4] & 0xFF;
                const std::uint32_t v2 = (gp0_params_[4] >> 8) & 0xFF;
                const std::uint32_t u3 = gp0_params_[7] & 0xFF;
                const std::uint32_t v3 = (gp0_params_[7] >> 8) & 0xFF;
                draw_gouraud_textured_triangle(
                    x1, y1, u1, v1, gp0_params_[0],
                    x2, y2, u2, v2, gp0_params_[3],
                    x3, y3, u3, v3, gp0_params_[6],
                    gp0_params_[1] >> 16, (opcode & 0x02) != 0);
                if (quad) {
                    const std::uint32_t x4 = gp0_params_[11] & 0xFFFF;
                    const std::uint32_t y4 = gp0_params_[11] >> 16;
                    const std::uint32_t u4 = gp0_params_[10] & 0xFF;
                    const std::uint32_t v4 = (gp0_params_[10] >> 8) & 0xFF;
                    draw_gouraud_textured_triangle(
                        x1, y1, u1, v1, gp0_params_[0],
                        x3, y3, u3, v3, gp0_params_[6],
                        x4, y4, u4, v4, gp0_params_[9],
                        gp0_params_[1] >> 16, (opcode & 0x02) != 0);
                }
            } else {
                const std::uint32_t x1 = gp0_params_[1] & 0xFFFF;
                const std::uint32_t y1 = gp0_params_[1] >> 16;
                const std::uint32_t x2 = gp0_params_[3] & 0xFFFF;
                const std::uint32_t y2 = gp0_params_[3] >> 16;
                const std::uint32_t x3 = gp0_params_[5] & 0xFFFF;
                const std::uint32_t y3 = gp0_params_[5] >> 16;
                draw_gouraud_triangle(
                    x1, y1, gp0_params_[0],
                    x2, y2, gp0_params_[2],
                    x3, y3, gp0_params_[4],
                    (opcode & 0x02) != 0);
                if (quad) {
                    const std::uint32_t x4 = gp0_params_[7] & 0xFFFF;
                    const std::uint32_t y4 = gp0_params_[7] >> 16;
                    draw_gouraud_triangle(
                        x1, y1, gp0_params_[0],
                        x3, y3, gp0_params_[4],
                        x4, y4, gp0_params_[6],
                        (opcode & 0x02) != 0);
                }
            }
        } else if (opcode >= 0x40 && opcode <= 0x5F) {
            const std::uint32_t x1 = gp0_params_[0] & 0xFFFF;
            const std::uint32_t y1 = gp0_params_[0] >> 16;
            const std::uint32_t x2 = gp0_params_[1] & 0xFFFF;
            const std::uint32_t y2 = gp0_params_[1] >> 16;
            const std::uint16_t color = gpu_color(gp0_command);
            const std::int32_t dx = static_cast<std::int32_t>(x2) - static_cast<std::int32_t>(x1);
            const std::int32_t dy = static_cast<std::int32_t>(y2) - static_cast<std::int32_t>(y1);
            const std::uint32_t steps = std::max(std::abs(dx), std::abs(dy));
            for (std::uint32_t step = 0; step <= steps; ++step) {
                const std::int32_t x = steps == 0 ? x1 : static_cast<std::int32_t>(x1) + dx * step / steps;
                const std::int32_t y = steps == 0 ? y1 : static_cast<std::int32_t>(y1) + dy * step / steps;
                if (x >= 0 && y >= 0) put_pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), color);
            }
        } else if (opcode >= 0x60 && opcode <= 0x7F) {
            const std::uint32_t x = gp0_params_[0] & 0xFFFF;
            const std::uint32_t y = gp0_params_[0] >> 16;
            const std::uint32_t width = gp0_params_[1] & 0xFFFF;
            const std::uint32_t height = gp0_params_[1] >> 16;
            fill_rectangle(x, y, width, height, gpu_color(gp0_command));
        } else if (opcode == 0xA0) {
            gp0_transfer_x = gp0_params_[0] & 0xFFFF;
            gp0_transfer_y = gp0_params_[0] >> 16;
            gp0_transfer_width = gp0_params_[1] & 0xFFFF;
            gp0_transfer_height = gp0_params_[1] >> 16;
            gp0_transfer_start_x = gp0_transfer_x;
            gp0_transfer_start_y = gp0_transfer_y;
            gp0_transfer_cursor = 0;
            gp0_transfer_remaining = gp0_transfer_width * gp0_transfer_height;
            gpu_status &= ~0x04000000u;
        } else if (opcode == 0xC0) {
            gpu_read_x = gp0_params_[0] & 0xFFFF;
            gpu_read_y = gp0_params_[0] >> 16;
            gpu_read_width = gp0_params_[1] & 0xFFFF;
            gpu_read_height = gp0_params_[1] >> 16;
            gpu_read_remaining = gpu_read_width * gpu_read_height;
            gpu_read_cursor = 0;
            gpu_status |= 0x08000000u;
        } else if (opcode == 0x80) {
            const std::uint32_t source_x = gp0_params_[0] & 0xFFFF;
            const std::uint32_t source_y = gp0_params_[0] >> 16;
            const std::uint32_t destination_x = gp0_params_[1] & 0xFFFF;
            const std::uint32_t destination_y = gp0_params_[1] >> 16;
            const std::uint32_t width = gp0_params_[2] & 0xFFFF;
            const std::uint32_t height = gp0_params_[2] >> 16;
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::uint32_t source_x_wrapped = (source_x + x) & 1023u;
                    const std::uint32_t source_y_wrapped = (source_y + y) & 511u;
                    const std::uint32_t destination_x_wrapped = (destination_x + x) & 1023u;
                    const std::uint32_t destination_y_wrapped = (destination_y + y) & 511u;
                    vram_[destination_y_wrapped * 1024 + destination_x_wrapped]
                        = vram_[source_y_wrapped * 1024 + source_x_wrapped];
                }
            }
        } else if (opcode == 0xE1) {
            gpu_status = (gpu_status & ~0x7FFu) | (gp0_params_[0] & 0x7FFu);
        } else if (opcode == 0xE2) {
            gpu_tex_window_x = gp0_params_[0] & 0x1F;
            gpu_tex_window_y = (gp0_params_[0] >> 5) & 0x1F;
            gpu_tex_window_mask_x = (gp0_params_[0] >> 10) & 0x1F;
            gpu_tex_window_mask_y = (gp0_params_[0] >> 15) & 0x1F;
            gpu_tex_window_offset_x = (gp0_params_[0] >> 20) & 0x1F;
            gpu_tex_window_offset_y = (gp0_params_[0] >> 25) & 0x1F;
        } else if (opcode == 0xE3) {
            gpu_draw_x1 = gp0_params_[0] & 0xFFFF;
            gpu_draw_y1 = gp0_params_[0] >> 16;
        } else if (opcode == 0xE4) {
            gpu_draw_x2 = gp0_params_[0] & 0xFFFF;
            gpu_draw_y2 = gp0_params_[0] >> 16;
        } else if (opcode == 0xE5) {
            gpu_draw_offset_x = gp0_params_[0] & 0xFFFF;
            gpu_draw_offset_y = gp0_params_[0] >> 16;
        } else if (opcode == 0xE6) {
            gpu_mask_set = gp0_params_[0] & 1;
            gpu_mask_keep = (gp0_params_[0] >> 1) & 1;
        }

        gp0_param_count = 0;
        gp0_expected = 0;
        gp0_command = 0;
    }

    void gp1_store(std::uint32_t word)
    {
        if (std::getenv("ANYPS1_GPU_TRACE") != nullptr) {
            std::fprintf(stderr, "GP1=0x%08X display=%ux%u+%u,%u pc=0x%08X\n",
                static_cast<unsigned>(word),
                static_cast<unsigned>(display_width),
                static_cast<unsigned>(display_height),
                static_cast<unsigned>(display_x),
                static_cast<unsigned>(display_y),
                static_cast<unsigned>(pc_));
        }
        const std::uint32_t command = word >> 24;
        const std::uint32_t parameter = word & 0xFFFFFF;
        if (command == 0x00) {
            gp0_param_count = 0;
            gp0_expected = 0;
            gp0_command = 0;
            gp0_transfer_remaining = 0;
            gpu_status = 0x1F400000;
            display_x = 0;
            display_y = 0;
            display_width = 320;
            display_height = 240;
            display_depth = 15;
        } else if (command == 0x01) {
            gp0_param_count = 0;
            gp0_expected = 0;
            gp0_command = 0;
        } else if (command == 0x02) {
            gpu_status &= ~0x08000000u;
        } else if (command == 0x03) {
            gpu_status = (gpu_status & ~0x00800000u) | ((parameter & 1) ? 0 : 0x00800000u);
        } else if (command == 0x04) {
            gpu_status = (gpu_status & ~0x600u) | ((parameter & 3u) << 9);
        } else if (command == 0x05) {
            display_x = parameter & 0x3FF;
            display_y = (parameter >> 10) & 0x1FF;
        } else if (command == 0x06) {
            display_width = ((parameter >> 12) & 0xFFF) - (parameter & 0xFFF) + 1;
        } else if (command == 0x07) {
            display_height = ((parameter >> 10) & 0x3FF) - (parameter & 0x3FF) + 1;
        } else if (command == 0x08) {
            gpu_status = (gpu_status & ~0x0007FFFFu) | (parameter & 0x0007FFFFu);
            const std::uint32_t width_code = parameter & 3;
            const std::uint32_t height = (parameter >> 2) & 1;
            display_depth = (parameter & 0x10u) ? 24 : ((parameter & 0x04u) ? 16 : 15);
            display_width = (width_code == 1) ? 368 : ((width_code == 2) ? 512 : ((width_code == 3) ? 640 : 256));
            if (height == 0) {
                display_height = display_height <= 256 ? 240 : display_height;
            } else {
                display_height = 480;
            }
        } else if (command == 0x09) {
            gpu_status = (gpu_status & ~0x02000000u) | ((parameter & 1) ? 0 : 0x02000000u);
        } else if (command == 0x10) {
            switch (parameter) {
            case 0x02:
                gpuread = gpu_tex_window_x
                    | (gpu_tex_window_y << 5)
                    | (gpu_tex_window_mask_x << 10)
                    | (gpu_tex_window_mask_y << 15)
                    | (gpu_tex_window_offset_x << 20)
                    | (gpu_tex_window_offset_y << 25);
                break;
            case 0x03:
                gpuread = gpu_draw_x1 | (gpu_draw_y1 << 16);
                break;
            case 0x04:
                gpuread = gpu_draw_x2 | (gpu_draw_y2 << 16);
                break;
            case 0x05:
                gpuread = gpu_draw_offset_x | (gpu_draw_offset_y << 16);
                break;
            default:
                gpuread = 0;
                break;
            }
        }
    }

