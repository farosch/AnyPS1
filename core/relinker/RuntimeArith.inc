    void mult(std::int32_t left, std::int32_t right)
    {
        std::int64_t product = static_cast<std::int64_t>(left) * right;
        lo_ = static_cast<std::uint32_t>(product);
        hi_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
    }

    void mult(std::uint32_t left, std::uint32_t right)
    {
        std::uint64_t product = static_cast<std::uint64_t>(left) * right;
        lo_ = static_cast<std::uint32_t>(product);
        hi_ = static_cast<std::uint32_t>(product >> 32);
    }

    void div(std::int32_t left, std::int32_t right)
    {
        if (right == 0) {
            lo_ = left >= 0 ? 0xFFFFFFFF : 1;
            hi_ = left;
        } else if (left == -2147483648 && right == -1) {
            lo_ = 0x80000000;
            hi_ = 0;
        } else {
            lo_ = static_cast<std::uint32_t>(left / right);
            hi_ = static_cast<std::uint32_t>(left % right);
        }
    }

    void divu(std::uint32_t left, std::uint32_t right)
    {
        if (right == 0) {
            lo_ = 0xFFFFFFFF;
            hi_ = left;
        } else {
            lo_ = left / right;
            hi_ = left % right;
        }
    }
