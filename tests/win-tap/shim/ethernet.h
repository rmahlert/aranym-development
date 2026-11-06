#pragma once
class ETHERNETDriver {
public:
    class Handler {
    public:
        int ethX;
        bool debug;
        explicit Handler(int index) : ethX(index), debug(false) {}
        virtual ~Handler() {}
        virtual bool open() = 0;
        virtual void close() = 0;
        virtual int recv(uint8 *, int) = 0;
        virtual int send(const uint8 *, int) = 0;
    };
};
