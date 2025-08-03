#apt install g++-aarch64-linux-gnu
aarch64-linux-gnu-g++ -O2 -mcpu=cortex-a78+simd -fverbose-asm -save-temps test_neon.cpp
qemu-aarch64 -L /usr/aarch64-linux-gnu ./a.out
