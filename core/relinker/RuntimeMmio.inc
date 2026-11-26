    void update_interrupt_request()
    {
        if (interrupt_status & interrupt_mask) {
            cop0_[13] |= 0x0400;
        } else {
            cop0_[13] &= ~0x0400u;
        }
    }

    void dma_channel_store(std::uint32_t channel, std::uint32_t offset, std::uint32_t value)
    {
        if (channel >= 7) {
            return;
        }
        if (offset == 0) {
            dma_madr[channel] = value & 0x00FFFFFF;
        } else if (offset == 4) {
            dma_bcr[channel] = value;
        } else if (offset == 8) {
            dma_chcr[channel] = value;
            if ((value & 0x01000000) != 0 && channel == 2) {
                dma_gpu_linked(dma_madr[channel]);
            } else if ((value & 0x01000000) == 0 && (value & 0x1) != 0) {
                dma_block(channel);
            } else if (channel == 6 && (value & 0x01000201) == 0x01000201) {
                dma_otc(dma_madr[channel], dma_bcr[channel]);
            }
        }
    }

    void dma_block(std::uint32_t channel)
    {
        const std::uint32_t word_count = dma_bcr[channel] & 0xFFFF;
        const std::uint32_t block_count = (dma_bcr[channel] >> 16) & 0xFFFF;
        const std::uint32_t count = block_count ? word_count * block_count : word_count;
        std::uint32_t address = dma_madr[channel];
        for (std::uint32_t index = 0; index < count && address < ram_.size(); ++index) {
            if (channel == 2) {
                gp0_store(read_ram32(address | 0x80000000));
            } else if (channel == 4) {
                spu_dma_write(address);
            }
            address = (address + 4) & 0x00FFFFFF;
        }
        dma_chcr[channel] &= ~0x01000000u;
        interrupt_status |= (1u << 3);
        update_interrupt_request();
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
        interrupt_status |= (1u << 3);
        update_interrupt_request();
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
        }
    }

    void start_joy_transfer(std::uint32_t command)
    {
        const unsigned char byte = static_cast<unsigned char>(command);
        joy_rx_count = 0;
        joy_rx_cursor = 0;
        if (byte == 0x01) {
            joy_rx[joy_rx_count++] = 0xFF;
        } else if (byte == 0x42) {
            joy_rx[joy_rx_count++] = 0x41;
            joy_rx[joy_rx_count++] = 0x5A;
            joy_rx[joy_rx_count++] = static_cast<unsigned char>(pad_state);
            joy_rx[joy_rx_count++] = static_cast<unsigned char>(pad_state >> 8);
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
            interrupt_status |= 1u << 2;
            update_interrupt_request();
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
            cdrom_set_response(cdrom_status);
            break;
        case 0x06:
            cdrom_set_response(cdrom_status, 0);
            break;
        case 0x08:
            cdrom_set_response(cdrom_status);
            break;
        case 0x09:
            cdrom_set_response(cdrom_status);
            cdrom_status = 0;
            break;
        case 0x0A:
            cdrom_set_response(cdrom_status);
            cdrom_status |= 0x20;
            break;
        case 0x0C:
            cdrom_status |= 1;
            cdrom_set_response(cdrom_status);
            break;
        case 0x0E:
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
            joy_ctrl = value;
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
            if (offset == 0xD88) {
                spu_control = value;
                spu_status = (spu_control & 0xFFFF);
            } else if (offset == 0xD8A) {
                spu_status = value;
            } else if (offset == 0xD80) {
                spu_main_volume_l = value & 0xFFFF;
            } else if (offset == 0xD82) {
                spu_main_volume_r = value & 0xFFFF;
            } else if (offset == 0xD8C) {
                spu_key_on = value;
                for (std::uint32_t voice = 0; voice < 24; ++voice) {
                    if (spu_key_on & (1u << voice)) {
                        spu_voice_adsr[voice] = 0x4000;
                    }
                }
            } else if (offset == 0xD90) {
                spu_key_off = value;
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
            interrupt_mask = value & 0x7FF;
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
                dicr = (dicr & ~0x7F000000u) | (value & 0x7F000000u);
                if ((value & 0x00800000) != 0) {
                    dicr &= ~(value & 0x007FFFFFu);
                } else {
                    dicr |= value & 0x007FFFFFu;
                }
            } else {
                const std::uint32_t channel = (physical - 0x1F801080) / 16;
                const std::uint32_t offset = (physical - 0x1F801080) % 16;
                dma_channel_store(channel, offset, value);
            }
            return true;
        }
        if (physical >= 0x1F801000 && physical < 0x1F803000) {
            return true;
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
        if (physical == 0x1F801810) {
            value = 0;
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
            if (offset == 0xD88) value = spu_control;
            else if (offset == 0xD8A) value = spu_status;
            else if (offset == 0xD8C) value = spu_key_on;
            else if (offset == 0xD90) value = spu_key_off;
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
            value = 0;
            return true;
        }
        return false;
    }

