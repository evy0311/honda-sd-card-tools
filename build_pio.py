Import("env")

ret = env.Execute("~/.platformio/packages/tool-wizio-pico/linux_x86_64/pioasm src/sd_rx.pio include/sd_rx.pio.h")

if ret:
    raise Exception("Unable to compile PIO") 

