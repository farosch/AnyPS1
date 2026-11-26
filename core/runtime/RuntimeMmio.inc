    void update_interrupt_request()
    {
        if (interrupt_status & interrupt_mask) {
            cop0_[13] |= 0x0400;
        } else {
            cop0_[13] &= ~0x0400u;
        }
    }

    void assert_interrupt(std::uint32_t line)
    {
        if (line < 13) {
            interrupt_status |= 1u << line;
        }
        update_interrupt_request();
    }

    void complete_dma(std::uint32_t channel)
    {
        if (channel < 7) {
            dicr |= 1u << (24u + channel);
            if (((dicr >> (16u + channel)) & 1u) != 0
                && (dicr & 0x00800000u) != 0) {
                dicr |= 0x80000000u;
                assert_interrupt(3);
            }
        }
    }

    void timer_irq(std::uint32_t timer)
    {
        const std::uint32_t mode = timer_mode[timer];
        if (timer_irq_active[timer] && (mode & 64) == 0) {
            return;
        }
        timer_irq_active[timer] = true;
        if ((mode & 128) != 0) {
            timer_mode[timer] ^= 0x400;
        } else {
            timer_mode[timer] |= 0x400;
        }
        assert_interrupt(10u + timer);
    }

    void tick_timer(std::uint32_t timer)
    {
        const std::uint32_t mode = timer_mode[timer];
        if ((mode & 1) != 0) {
            const std::uint32_t sync_mode = (mode >> 1) & 3;
            if (sync_mode == 0) {
                timer_gate_active[timer] = false;
            } else if (sync_mode == 1) {
                if (timer_gate_active[timer]) {
                    timer_count[timer] = 0;
                }
            } else if (sync_mode == 2) {
                if (!timer_gate_active[timer]) {
                    timer_count[timer] = 0;
                    timer_gate_active[timer] = false;
                    return;
                }
            } else if (sync_mode == 3) {
                if (timer_gate_active[timer]) {
                    timer_count[timer] = 0;
                    timer_gate_active[timer] = false;
                    return;
                }
            }
        }

        bool clocked = true;
        if (timer == 0 && (mode & 0x100) != 0) {
            clocked = hblank_active;
        } else if (timer == 1 && (mode & 0x100) != 0) {
            clocked = hblank_active;
        } else if (timer == 2 && (mode & 0x200) != 0) {
            clocked = true;
        }
        if (!clocked) {
            return;
        }

        ++timer_count[timer];
        if (timer_target[timer] != 0 && timer_count[timer] >= timer_target[timer]) {
            if ((mode & 8) != 0) {
                timer_count[timer] = 0;
            }
            if ((mode & 16) != 0) {
                timer_irq(timer);
            }
        }
        if (timer_count[timer] >= 0x10000) {
            timer_count[timer] = 0;
            if ((mode & 32) != 0) {
                timer_irq(timer);
            }
        }
    }

    void tick_timers()
    {
        tick_timer(0);
        tick_timer(1);
        tick_timer(2);
    }

    void dma_channel_store(std::uint32_t channel, std::uint32_t offset, std::uint32_t value)
    {
        if (std::getenv("ANYPS1_DMA_TRACE") != nullptr) {
            std::fprintf(stderr, "DMA channel=%u offset=0x%X value=0x%08X pc=0x%08X\n",
                static_cast<unsigned>(channel),
                static_cast<unsigned>(offset),
                static_cast<unsigned>(value),
                static_cast<unsigned>(pc_));
        }
        if (channel >= 7) {
            return;
        }
        if (offset == 0) {
            dma_madr[channel] = value & 0x00FFFFFF;
        } else if (offset == 4) {
            dma_bcr[channel] = value;
        } else if (offset == 8) {
            dma_chcr[channel] = value;
            if (channel == 2
                && (value & 0x01000000u) != 0
                && ((value >> 8) & 3u) == 2) {
                dma_gpu_linked(dma_madr[channel]);
            } else if (channel == 2
                && (value & 0x01000000u) != 0
                && ((value >> 8) & 3u) == 1) {
                dma_block(channel);
            } else if (channel == 6
                && (value & 0x01000201u) == 0x01000201u) {
                dma_otc(dma_madr[channel], dma_bcr[channel]);
            } else if ((value & 0x01000000u) != 0
                && (value & 1u) != 0) {
                dma_block(channel);
            }
        }
    }

    void dma_block(std::uint32_t channel)
    {
        const std::uint32_t word_count = dma_bcr[channel] & 0xFFFF;
        const std::uint32_t block_count = (dma_bcr[channel] >> 16) & 0xFFFF;
        const std::uint32_t count = word_count == 0
            ? (block_count == 0 ? 1u : block_count)
            : (block_count == 0 ? word_count : word_count * block_count);
        std::uint32_t address = dma_madr[channel];
        for (std::uint32_t index = 0; index < count && address < ram_.size(); ++index) {
            if (channel == 2) {
                gp0_store(read_ram32(address | 0x80000000));
            } else if (channel == 4) {
                spu_dma_write(address);
            } else if (channel == 3 && cdrom_read_active) {
                const std::uint64_t disc_offset =
                    static_cast<std::uint64_t>(cdrom_lba) * 2352ULL;
                const std::uint64_t sector_offset =
                    disc_offset + 24ULL + static_cast<std::uint64_t>(index) * 4ULL;
                if (sector_offset + 4 <= cdrom_data.size()) {
                    write_ram32(address,
                        static_cast<std::uint32_t>(cdrom_data[sector_offset])
                        | (static_cast<std::uint32_t>(cdrom_data[sector_offset + 1]) << 8)
                        | (static_cast<std::uint32_t>(cdrom_data[sector_offset + 2]) << 16)
                        | (static_cast<std::uint32_t>(cdrom_data[sector_offset + 3]) << 24));
                }
            }
            address = ((dma_chcr[channel] & 0x40000000u) != 0)
                ? (address - 4) & 0x00FFFFFF
                : (address + 4) & 0x00FFFFFF;
        }
        if (channel == 3 && cdrom_read_active) {
            ++cdrom_lba;
        }
        dma_chcr[channel] &= ~0x01000000u;
        complete_dma(channel);
    }

    void dma_gpu_linked(std::uint32_t address)
    {
        std::uint32_t visited = 0;
        for (; visited++ < 4096;) {
            address &= 0x00FFFFFF;
            if (address >= ram_.size()) {
                break;
            }
            const std::uint32_t header = read_ram32(address | 0x80000000);
            const std::uint32_t size = header >> 24;
            const std::uint32_t next = header & 0x00FFFFFF;
            for (std::uint32_t index = 1; index <= size && address + index * 4 < ram_.size(); ++index) {
                gp0_store(read_ram32((address + index * 4) | 0x80000000));
            }
            if ((header & 0x00800000) != 0 || next == 0xFFFFFF) {
                break;
            }
            address = next;
        }
        dma_chcr[2] &= ~0x01000000u;
        complete_dma(2);
    }

    void dma_otc(std::uint32_t address, std::uint32_t bcr)
    {
        const std::uint32_t count = bcr & 0xFFFF;
        for (std::uint32_t index = 0; index < count && address < ram_.size(); ++index) {
            const std::uint32_t value = (index + 1 >= count) ? 0xFFFFFF : (address - 4) & 0xFFFFFF;
            ram_[address] = static_cast<unsigned char>(value);
            ram_[address + 1] = static_cast<unsigned char>(value >> 8);
            ram_[address + 2] = static_cast<unsigned char>(value >> 16);
            ram_[address + 3] = static_cast<unsigned char>(value >> 24);
            address -= 4;
        }
        dma_chcr[6] &= ~0x01000000u;
    }

    void spu_dma_write(std::uint32_t address)
    {
        if (address < ram_.size()) {
            spu_dma_word = read_ram32(address | 0x80000000);
            std::uint32_t spu_offset = spu_dma_address * 8u;
            for (std::uint32_t byte = 0; byte < 4; ++byte) {
                spu_ram[(spu_offset + byte) & (spu_ram.size() - 1)]
                    = static_cast<unsigned char>(spu_dma_word >> (byte * 8));
            }
            spu_dma_address = (spu_dma_address + 1) & 0x7FFF;
        }
    }

    void start_joy_transfer(std::uint32_t command)
    {
        const unsigned char byte = static_cast<unsigned char>(command);
        joy_rx_count = 0;
        joy_rx_cursor = 0;
        if (joy_motor_phase == 1) {
            joy_motor_small_ = byte;
            joy_motor_phase = 2;
            joy_rx[joy_rx_count++] = 0xFF;
        } else if (joy_motor_phase == 2) {
            joy_motor_large_ = byte;
            joy_motor_phase = 0;
            joy_rx[joy_rx_count++] = 0xFF;
        } else if (joy_config_mode && byte == 0x01) {
            joy_analog_mode = true;
            joy_rx[joy_rx_count++] = 0xF3;
            joy_rx[joy_rx_count++] = 0x5A;
            joy_rx[joy_rx_count++] = 0x01;
            joy_rx[joy_rx_count++] = 0x02;
        } else if (joy_config_mode && byte == 0x02) {
            joy_analog_mode = false;
            joy_rx[joy_rx_count++] = 0xF3;
            joy_rx[joy_rx_count++] = 0x5A;
            joy_rx[joy_rx_count++] = 0x01;
            joy_rx[joy_rx_count++] = 0x02;
        } else if (byte == 0x01) {
            joy_rx[joy_rx_count++] = 0xFF;
        } else if (byte == 0x42) {
            if (joy_analog_mode) {
                joy_rx[joy_rx_count++] = 0x73;
                joy_rx[joy_rx_count++] = 0x5A;
                joy_rx[joy_rx_count++] = 0x01;
                joy_rx[joy_rx_count++] = 0x02;
                joy_rx[joy_rx_count++] = static_cast<unsigned char>(pad_state);
                joy_rx[joy_rx_count++] = static_cast<unsigned char>(pad_state >> 8);
                joy_rx[joy_rx_count++] = controller_state.right_x;
                joy_rx[joy_rx_count++] = controller_state.right_y;
                joy_motor_small_ = controller_state.right_x;
                joy_motor_phase = 1;
            } else {
                joy_rx[joy_rx_count++] = 0x41;
                joy_rx[joy_rx_count++] = 0x5A;
                joy_rx[joy_rx_count++] = static_cast<unsigned char>(pad_state);
                joy_rx[joy_rx_count++] = static_cast<unsigned char>(pad_state >> 8);
            }
        } else if (byte == 0x43) {
            joy_rx[joy_rx_count++] = 0xF3;
            joy_rx[joy_rx_count++] = 0x5A;
            joy_rx[joy_rx_count++] = 0x01;
            joy_rx[joy_rx_count++] = 0x02;
        } else if (byte == 0x44) {
            joy_config_mode = true;
            joy_ctrl |= 0x2000;
            joy_rx[joy_rx_count++] = 0xF3;
            joy_rx[joy_rx_count++] = 0x5A;
            joy_rx[joy_rx_count++] = 0x01;
            joy_rx[joy_rx_count++] = 0x02;
        } else if (byte == 0x45) {
            joy_config_mode = false;
            joy_rx[joy_rx_count++] = 0xF3;
            joy_rx[joy_rx_count++] = 0x5A;
            joy_rx[joy_rx_count++] = 0x01;
            joy_rx[joy_rx_count++] = 0x02;
        } else if (byte == 0x46) {
            joy_rx[joy_rx_count++] = 0xF3;
            joy_rx[joy_rx_count++] = 0x5A;
            joy_rx[joy_rx_count++] = 0x01;
            joy_rx[joy_rx_count++] = 0x02;
        } else if (byte == 0x81) {
            joy_rx[joy_rx_count++] = 0xFF;
            memory_card_phase = 1;
        } else if (byte == 0x52) {
            joy_rx[joy_rx_count++] = 0x5A;
            joy_rx[joy_rx_count++] = 0x5D;
            joy_rx[joy_rx_count++] = 0x00;
            joy_rx[joy_rx_count++] = 0x00;
            if (memory_card_phase == 1) {
                memory_card_phase = 2;
                memory_card_writing = false;
            }
        } else if (byte == 0x57) {
            joy_rx[joy_rx_count++] = 0x5A;
            joy_rx[joy_rx_count++] = 0x5D;
            joy_rx[joy_rx_count++] = 0x00;
            joy_rx[joy_rx_count++] = 0x00;
            if (memory_card_phase == 1) {
                memory_card_phase = 2;
                memory_card_writing = true;
            }
        } else if (memory_card_phase == 2) {
            memory_card_address = static_cast<std::uint32_t>(byte) << 8;
            memory_card_phase = 3;
            joy_rx[joy_rx_count++] = 0x5A;
        } else if (memory_card_phase == 3) {
            memory_card_address |= byte;
            memory_card_address *= 128u;
            memory_card_phase = 4;
            joy_rx[joy_rx_count++] = 0x5C;
            joy_rx[joy_rx_count++] = 0x5D;
        } else if (memory_card_phase == 4) {
            if (memory_card_writing) {
                if (memory_card_address < memory_card.size()) {
                    memory_card[memory_card_address++] = byte;
                    ++memory_card_write_count;
                }
                joy_rx[joy_rx_count++] = 0x5C;
                joy_rx[joy_rx_count++] = 0x5D;
            } else {
                if (memory_card_address < memory_card.size()) {
                    joy_rx[joy_rx_count++] = memory_card[memory_card_address++];
                } else {
                    joy_rx[joy_rx_count++] = 0xFF;
                }
            }
            memory_card_phase = 0;
        }
        joy_stat |= 0x00000004;
    }

    void cdrom_set_response(unsigned char first, unsigned char second = 0)
    {
        cdrom_response_count = second ? 2 : 1;
        cdrom_response_cursor = 0;
        cdrom_response[0] = first;
        cdrom_response[1] = second;
        cdrom_interrupt = 3;
        if ((cdrom_interrupt_enable & 3) != 0) {
            assert_interrupt(2);
        }
    }

    void cdrom_execute(std::uint32_t command)
    {
        cdrom_param_count = 0;
        switch (command & 0xFF) {
        case 0x01:
            cdrom_set_response(cdrom_status);
            break;
        case 0x02:
            if (cdrom_params[0] != 0 && cdrom_params[0] <= 0x99) {
                const std::uint32_t minute = (cdrom_params[0] >> 4) * 10 + (cdrom_params[0] & 15);
                const std::uint32_t second = (cdrom_params[1] >> 4) * 10 + (cdrom_params[1] & 15);
                const std::uint32_t frame = (cdrom_params[2] >> 4) * 10 + (cdrom_params[2] & 15);
                cdrom_lba = minute * 60 * 75 + second * 75 + frame;
                cdrom_status |= 1;
            }
            cdrom_set_response(cdrom_status);
            break;
        case 0x06:
            cdrom_read_active = true;
            cdrom_status |= 0x21;
            cdrom_set_response(cdrom_status);
            break;
        case 0x1B:
            cdrom_read_active = true;
            cdrom_status |= 0x20;
            cdrom_set_response(cdrom_status);
            break;
        case 0x08:
            cdrom_set_response(cdrom_status);
            break;
        case 0x09:
            cdrom_read_active = false;
            cdrom_set_response(cdrom_status);
            cdrom_status = 0;
            break;
        case 0x0A:
            cdrom_set_response(cdrom_status);
            cdrom_status |= 0x20;
            break;
        case 0x0E:
            if (cdrom_params[0] & 0x20) cdrom_sector_size = 2340;
            else if (cdrom_params[0] & 0x80) cdrom_sector_size = 2336;
            else cdrom_sector_size = 2048;
            cdrom_set_response(cdrom_status);
            break;
        case 0x0C:
            cdrom_status |= 1;
            cdrom_set_response(cdrom_status);
            break;
        case 0x14:
            cdrom_status |= 1;
            cdrom_set_response(cdrom_status);
            break;
        case 0x15:
            cdrom_set_response(cdrom_status);
            cdrom_response_count = 3;
            cdrom_response[1] = 0x00;
            cdrom_response[2] = 0x20;
            break;
        case 0x19:
            cdrom_set_response(cdrom_status);
            cdrom_response_count = 8;
            for (std::uint32_t index = 1; index < cdrom_response_count; ++index) {
                cdrom_response[index] = 0;
            }
            break;
        case 0x1A:
            cdrom_set_response(cdrom_status);
            cdrom_response_count = 1;
            break;
        default:
            cdrom_set_response(cdrom_status);
            break;
        }
    }

    bool io_store(std::uint32_t address, std::uint32_t value, std::uint32_t size)
    {
        std::uint32_t physical = address & 0x1FFFFFFF;
        if (physical >= 0x1F800000 && physical < 0x1F800400) {
            std::uint32_t offset = physical - 0x1F800000;
            for (std::uint32_t index = 0; index < size && offset + index < scratchpad_.size(); ++index) {
                scratchpad_[offset + index] = static_cast<unsigned char>(value >> (index * 8));
            }
            return true;
        }
        if (physical == 0x1F801020 || physical == 0x1F801024
            || physical == 0x1F801060 || physical == 0x1F801064) {
            return true;
        }
        if (physical == 0x1F801810) {
            gp0_store(value);
            return true;
        }
        if (physical == 0x1F801814) {
            gp1_store(value);
            return true;
        }
        if (physical == 0x1F801040) {
            start_joy_transfer(value);
            return true;
        }
        if (physical == 0x1F801044) {
            joy_stat = value;
            joy_stat |= 0x00000005;
            return true;
        }
        if (physical == 0x1F801048) {
            joy_mode = value;
            return true;
        }
        if (physical == 0x1F80104A) {
            joy_ctrl = value & 0xFFFF;
            return true;
        }
        if (physical == 0x1F80104E) {
            joy_baud = value;
            return true;
        }
        if (physical >= 0x1F801800 && physical < 0x1F801804) {
            const std::uint32_t offset = physical - 0x1F801800;
            if (offset == 0) {
                cdrom_index = value & 3;
            } else if (offset == 1) {
                if (cdrom_index == 0) {
                    cdrom_execute(value);
                } else if (cdrom_index == 1) {
                    cdrom_interrupt_enable = value & 0x1F;
                }
            } else if (offset == 2) {
                if (cdrom_index == 0) {
                    cdrom_params[cdrom_param_count++ & 15] = static_cast<unsigned char>(value);
                } else if (cdrom_index == 1) {
                    cdrom_interrupt &= ~static_cast<std::uint32_t>(value & 0x1F);
                }
            } else if (offset == 3) {
                if (cdrom_index == 1) {
                    cdrom_interrupt_enable = value & 0x1F;
                } else if (cdrom_index == 2) {
                    cdrom_param_count = 0;
                }
            }
            return true;
        }
        if (physical >= 0x1F801C00 && physical < 0x1F801E80) {
            const std::uint32_t offset = physical - 0x1F801C00;
            if (offset == 0x1AA) {
                spu_control = value;
                spu_status = (spu_control & 0xFFFF);
            } else if (offset == 0x1AE) {
                spu_status = value;
            } else if (offset == 0x180) {
                spu_main_volume_l = value & 0xFFFF;
            } else if (offset == 0x182) {
                spu_main_volume_r = value & 0xFFFF;
            } else if (offset == 0x188) {
                spu_key_on = value;
                for (std::uint32_t voice = 0; voice < 24; ++voice) {
                    if (spu_key_on & (1u << voice)) {
                        spu_voice_adsr[voice] = 0x4000;
                        spu_voice_phase[voice] = 1;
                        spu_voice_counter[voice] = 0;
                        spu_voice_address[voice] = spu_voice_start[voice] * 8u;
                        spu_voice_repeat_address[voice] = spu_voice_repeat[voice] * 8u;
                    }
                }
            } else if (offset == 0x18C) {
                spu_key_off = value;
                for (std::uint32_t voice = 0; voice < 24; ++voice) {
                    if (spu_key_off & (1u << voice)) {
                        spu_voice_phase[voice] = 4;
                        spu_key_on &= ~(1u << voice);
                    }
                }
            } else if (offset < 0x180) {
                const std::uint32_t voice = offset / 16;
                const std::uint32_t voice_offset = offset % 16;
                if (voice < 24) {
                    if (voice_offset == 0) spu_voice_volume_l[voice] = value & 0xFFFF;
                    else if (voice_offset == 2) spu_voice_volume_r[voice] = value & 0xFFFF;
                    else if (voice_offset == 4) spu_voice_adsr[voice] = value & 0xFFFF;
                    else if (voice_offset == 6) spu_voice_adsr[voice] = value & 0xFFFF;
                    else if (voice_offset == 8) spu_voice_start[voice] = value & 0xFFFF;
                    else if (voice_offset == 14) spu_voice_repeat[voice] = value & 0xFFFF;
                }
            } else if (offset == 0xDA6) {
                spu_dma_address = value & 0xFFFF;
            }
            return true;
        }
        if (physical == 0x1F801070) {
            interrupt_status &= value;
            update_interrupt_request();
            return true;
        }
        if (physical == 0x1F801074) {
            interrupt_mask = value & 0x1FFF;
            update_interrupt_request();
            return true;
        }
        if (physical >= 0x1F801100 && physical < 0x1F801128) {
            const std::uint32_t timer = (physical - 0x1F801100) / 16;
            const std::uint32_t offset = (physical - 0x1F801100) % 16;
            if (offset == 0) {
                timer_count[timer] = value;
            } else if (offset == 4) {
                timer_mode[timer] = value;
                timer_irq_active[timer] = false;
                if ((value & 4) != 0) {
                    timer_count[timer] = 0;
                }
            } else if (offset == 8) {
                timer_target[timer] = value & 0xFFFF;
            }
            return true;
        }
        if (physical >= 0x1F801080 && physical < 0x1F801100) {
            if (physical == 0x1F8010F0) {
                dpcr = value;
            } else if (physical == 0x1F8010F4) {
                dicr &= ~0x7F000000u;
                dicr = (dicr & 0xFF000000u) | (value & 0x00FF803Fu);
            } else {
                const std::uint32_t channel = (physical - 0x1F801080) / 16;
                const std::uint32_t offset = (physical - 0x1F801080) % 16;
                dma_channel_store(channel, offset, value);
            }
            return true;
        }
        if (physical >= 0x1F801000 && physical < 0x1F803000) {
            char detail[96];
            std::snprintf(detail, sizeof(detail),
                "store to unsupported PS1 MMIO 0x%08X (PC 0x%08X)",
                static_cast<unsigned>(address),
                static_cast<unsigned>(pc_));
            unsupported_ = detail;
            return false;
        }
        char detail[96];
        std::snprintf(detail, sizeof(detail),
            "store to unsupported PS1 memory 0x%08X (PC 0x%08X)",
            static_cast<unsigned>(address),
            static_cast<unsigned>(pc_));
        unsupported_ = detail;
        return false;
    }

    bool io_load(std::uint32_t address, std::uint32_t size, std::uint32_t& value)
    {
        std::uint32_t physical = address & 0x1FFFFFFF;
        if (physical >= 0x1F800000 && physical < 0x1F800400) {
            std::uint32_t offset = physical - 0x1F800000;
            value = 0;
            for (std::uint32_t index = 0; index < size && offset + index < scratchpad_.size(); ++index) {
                value |= static_cast<std::uint32_t>(scratchpad_[offset + index]) << (index * 8);
            }
            return true;
        }
        if (physical == 0x1F801020 || physical == 0x1F801024
            || physical == 0x1F801060 || physical == 0x1F801064) {
            value = 0;
            return true;
        }
        if (physical == 0x1F801810) {
            if (gpu_read_remaining != 0) {
                if (gpu_read_cursor < gpu_read_width * gpu_read_height) {
                    const std::uint32_t x = (gpu_read_x + gpu_read_cursor % gpu_read_width) & 1023u;
                    const std::uint32_t y = (gpu_read_y + gpu_read_cursor / gpu_read_width) & 511u;
                    const std::uint16_t low = (x < 1024 && y < 512) ? vram_[y * 1024 + x] : 0;
                    ++gpu_read_cursor;
                    --gpu_read_remaining;
                    if (gpu_read_cursor < gpu_read_width * gpu_read_height) {
                        const std::uint32_t x2 = (gpu_read_x + gpu_read_cursor % gpu_read_width) & 1023u;
                        const std::uint32_t y2 = (gpu_read_y + gpu_read_cursor / gpu_read_width) & 511u;
                        const std::uint16_t high = (x2 < 1024 && y2 < 512) ? vram_[y2 * 1024 + x2] : 0;
                        ++gpu_read_cursor;
                        --gpu_read_remaining;
                        value = static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(high) << 16);
                    } else {
                        value = low;
                    }
                    if (gpu_read_remaining == 0) {
                        gpu_status &= ~0x08000000u;
                    }
                } else {
                    value = 0;
                }
            } else {
                value = gpuread;
            }
            return true;
        }
        if (physical == 0x1F801814) {
            value = gpu_status;
            return true;
        }
        if (physical == 0x1F801040) {
            value = joy_rx_count ? joy_rx[joy_rx_cursor++] : 0xFFFF;
            if (joy_rx_cursor >= joy_rx_count) {
                joy_rx_cursor = joy_rx_count;
            }
            joy_stat |= 0x00000002;
            return true;
        }
        if (physical == 0x1F801044) {
            value = joy_stat;
            return true;
        }
        if (physical == 0x1F801048) {
            value = joy_mode;
            return true;
        }
        if (physical == 0x1F80104A) {
            value = joy_ctrl;
            return true;
        }
        if (physical == 0x1F80104E) {
            value = joy_baud;
            return true;
        }
        if (physical >= 0x1F801800 && physical < 0x1F801804) {
            const std::uint32_t offset = physical - 0x1F801800;
            if (offset == 0) {
                value = cdrom_status;
            } else if (offset == 1) {
                value = cdrom_response_cursor < cdrom_response_count
                    ? cdrom_response[cdrom_response_cursor++] : 0;
            } else if (offset == 2) {
                value = cdrom_param_count;
            } else {
                value = cdrom_interrupt | (cdrom_interrupt_enable << 8);
            }
            return true;
        }
        if (physical >= 0x1F801C00 && physical < 0x1F801E80) {
            const std::uint32_t offset = physical - 0x1F801C00;
            if (offset == 0x1AA) value = spu_control;
            else if (offset == 0x1AE) value = spu_status;
            else if (offset == 0x188) value = spu_key_on;
            else if (offset == 0x18C) value = spu_key_off;
            else value = 0;
            return true;
        }
        if (physical == 0x1F801070) {
            value = interrupt_status;
            return true;
        }
        if (physical == 0x1F801074) {
            value = interrupt_mask;
            return true;
        }
        if (physical >= 0x1F801100 && physical < 0x1F801128) {
            const std::uint32_t timer = (physical - 0x1F801100) / 16;
            const std::uint32_t offset = (physical - 0x1F801100) % 16;
            value = offset == 0 ? timer_count[timer] :
                    offset == 4 ? timer_mode[timer] :
                    timer_target[timer];
            return true;
        }
        if (physical >= 0x1F801080 && physical < 0x1F801100) {
            if (physical == 0x1F8010F0) {
                value = dpcr;
            } else if (physical == 0x1F8010F4) {
                value = dicr;
            } else {
                const std::uint32_t channel = (physical - 0x1F801080) / 16;
                const std::uint32_t offset = (physical - 0x1F801080) % 16;
                value = offset == 0 ? dma_madr[channel] :
                        offset == 4 ? dma_bcr[channel] :
                        dma_chcr[channel];
            }
            return true;
        }
        if (physical >= 0x1F801000 && physical < 0x1F803000) {
            unsupported_ = "load from unsupported PS1 MMIO";
            return false;
        }
        return false;
    }

