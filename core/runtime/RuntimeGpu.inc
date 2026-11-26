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
            switch (command) {
            case 0x01:
            case 0x00:
            case 0x30:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x / 16, -32768, 32767));
                gte_control[25] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y, -32768, 32767));
                gte_control[26] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(z, -32768, 32767));
                break;
            case 0x14:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x * x / 4096, -32768, 32767));
                gte_control[25] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y * y / 4096, -32768, 32767));
                gte_control[26] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(z * z / 4096, -32768, 32767));
                break;
            case 0x06:
            case 0x08:
                gte_control[24] = static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(
                        gte_data_[9] * gte_data_[10] - gte_data_[12] * gte_data_[13]
                        + gte_data_[14] * gte_data_[15],
                        -32768, 32767));
                break;
            case 0x2A:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    gte_data_[12] + gte_data_[13] + gte_data_[14], 0, 0xFFFF));
                break;
            case 0x2D:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    gte_data_[12] + gte_data_[13] + gte_data_[14] + gte_data_[15], 0, 0xFFFF));
                break;
            case 0x29:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    gte_data_[9] * gte_data_[10] / 4096, 0, 0xFFFF));
                break;
            case 0x2C:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    (gte_data_[9] + gte_data_[10] + gte_data_[11]) / 3, 0, 0xFFFF));
                break;
            case 0x1F:
            case 0x3D:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    gte_data_[9] * gte_data_[12] + gte_data_[10] * gte_data_[13]
                    + gte_data_[11] * gte_data_[14], -32768, 32767));
                break;
            case 0x3E:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    gte_data_[9] * gte_data_[12] / 4096, -32768, 32767));
                break;
            case 0x3F:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    gte_data_[9] + gte_data_[12], -32768, 32767));
                gte_control[25] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    gte_data_[10] + gte_data_[13], -32768, 32767));
                gte_control[26] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    gte_data_[11] + gte_data_[14], -32768, 32767));
            case 0x02:
                gte_control[24] = static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(
                        gte_data_[9] * gte_data_[10] - gte_data_[10] * gte_data_[11]
                        + gte_data_[11] * gte_data_[12],
                        -32768, 32767));
                break;
            case 0x05:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x + y + z, -32768, 32767));
                break;
            case 0x28:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x, 0, 255));
                gte_control[25] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y, 0, 255));
                gte_control[26] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(z, 0, 255));
                break;
            case 0x04:
                gte_control[24] = static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(z * gte_data_[9] - x * gte_data_[11], -32768, 32767));
                gte_control[25] = static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(z * gte_data_[10] - y * gte_data_[12], -32768, 32767));
                gte_control[26] = static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(x * gte_data_[11] - y * gte_data_[13], -32768, 32767));
                break;
            case 0x12:
                gte_control[24] = static_cast<std::uint32_t>(gte_data_[9]);
                gte_control[25] = static_cast<std::uint32_t>(gte_data_[10] & 0xFFFF);
                gte_control[26] = static_cast<std::uint32_t>(gte_data_[11]);
                break;
            case 0x0D:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x + y + z, -32768, 32767));
                gte_control[25] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y + z, -32768, 32767));
                gte_control[26] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(z, -32768, 32767));
                break;
            case 0x10:
            case 0x11:
            case 0x13:
            case 0x1B:
            case 0x1C:
            case 0x1D:
            case 0x1E:
            case 0x2E:
                gte_control[24] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x, -32768, 32767));
                gte_control[25] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y, -32768, 32767));
                gte_control[26] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(z, -32768, 32767));
                break;
            default:
                gte_control[24] = static_cast<std::uint32_t>(x);
                gte_control[25] = static_cast<std::uint32_t>(y);
                gte_control[26] = static_cast<std::uint32_t>(z);
                break;
            }
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

    void put_pixel(std::uint32_t x, std::uint32_t y, std::uint16_t color)
    {
        x = (gpu_draw_offset_x & 0x8000) ? x - (0x10000u - gpu_draw_offset_x) : x + gpu_draw_offset_x;
        y = (gpu_draw_offset_y & 0x8000) ? y - (0x10000u - gpu_draw_offset_y) : y + gpu_draw_offset_y;
        if (x >= gpu_draw_x1 && x <= gpu_draw_x2 && y >= gpu_draw_y1 && y <= gpu_draw_y2
            && x < 1024 && y < 512) {
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
        std::uint16_t color
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
                    put_pixel(x, y, color);
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
                const std::uint16_t value = static_cast<std::uint16_t>(
                    pixel == 0 ? word & 0xFFFF : word >> 16
                );
                gp0_transfer_x = (gp0_transfer_start_x + gp0_transfer_cursor % gp0_transfer_width) & 1023u;
                gp0_transfer_y = (gp0_transfer_start_y + gp0_transfer_cursor / gp0_transfer_width) & 511u;
                if (gp0_transfer_y < 512 && gp0_transfer_x < 1024) {
                    vram_[gp0_transfer_y * 1024 + gp0_transfer_x] = value;
                    ++gpu_vram_write_count;
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
                const std::uint32_t x1 = gp0_params_[0] & 0xFFFF;
                const std::uint32_t y1 = gp0_params_[0] >> 16;
                std::uint32_t param_index = textured ? 2 : 1;
                const std::uint32_t x2 = gp0_params_[param_index] & 0xFFFF;
                const std::uint32_t y2 = gp0_params_[param_index] >> 16;
                param_index += textured ? 2 : 1;
                const std::uint32_t x3 = gp0_params_[param_index] & 0xFFFF;
                const std::uint32_t y3 = gp0_params_[param_index] >> 16;
                const std::uint16_t color = gpu_color(gp0_command);
                draw_flat_triangle(x1, y1, x2, y2, x3, y3, color);
                if (quad) {
                    const std::uint32_t x4 = gp0_params_[textured ? 6 : 3] & 0xFFFF;
                    const std::uint32_t y4 = gp0_params_[textured ? 6 : 3] >> 16;
                    draw_flat_triangle(x1, y1, x3, y3, x4, y4, color);
                }
            } else if (textured) {
                const std::uint32_t x1 = gp0_params_[1] & 0xFFFF;
                const std::uint32_t y1 = gp0_params_[1] >> 16;
                const std::uint32_t x2 = gp0_params_[4] & 0xFFFF;
                const std::uint32_t y2 = gp0_params_[4] >> 16;
                const std::uint32_t x3 = gp0_params_[7] & 0xFFFF;
                const std::uint32_t y3 = gp0_params_[7] >> 16;
                const std::uint16_t color = gpu_color(gp0_params_[0]);
                draw_flat_triangle(x1, y1, x2, y2, x3, y3, color);
                if (quad) {
                    const std::uint32_t x4 = gp0_params_[10] & 0xFFFF;
                    const std::uint32_t y4 = gp0_params_[10] >> 16;
                    draw_flat_triangle(x1, y1, x3, y3, x4, y4, color);
                }
            } else {
                const std::uint32_t x1 = gp0_params_[1] & 0xFFFF;
                const std::uint32_t y1 = gp0_params_[1] >> 16;
                const std::uint32_t x2 = gp0_params_[3] & 0xFFFF;
                const std::uint32_t y2 = gp0_params_[3] >> 16;
                const std::uint32_t x3 = gp0_params_[5] & 0xFFFF;
                const std::uint32_t y3 = gp0_params_[5] >> 16;
                const std::uint16_t color = gpu_color(gp0_params_[0]);
                draw_flat_triangle(x1, y1, x2, y2, x3, y3, color);
                if (quad) {
                    const std::uint32_t x4 = gp0_params_[7] & 0xFFFF;
                    const std::uint32_t y4 = gp0_params_[7] >> 16;
                    draw_flat_triangle(x1, y1, x3, y3, x4, y4, color);
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

