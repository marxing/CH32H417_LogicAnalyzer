"""Read and control the CH32H417 board USB3 FSW3820 lane switch over EP1."""
import argparse
import ctypes
import time

DLL_PATH = r"E:\我的素材\逻辑分析仪\analysis\CH375DLL64.DLL"
CMD_GET_USB3_MUX = 0xC3
CMD_SET_USB3_MUX = 0xC4
MODES = {"auto": 0, "a": 1, "b": 2, "disabled": 3}
MODE_NAMES = {value: key for key, value in MODES.items()}


class Device:
    def __init__(self, index=0):
        self.index = index
        self.dll = ctypes.CDLL(DLL_PATH)
        u = ctypes.c_ulong
        p = ctypes.POINTER(u)
        d = self.dll
        d.CH375OpenDevice.argtypes = [u]
        d.CH375OpenDevice.restype = u
        d.CH375CloseDevice.argtypes = [u]
        d.CH375SetTimeoutEx.argtypes = [u, u, u, u, u]
        d.CH375SetTimeoutEx.restype = u
        d.CH375SetBufUploadEx.argtypes = [u, u, u, u]
        d.CH375SetBufUploadEx.restype = u
        d.CH375SetBufDownloadEx.argtypes = [u, u, u, u]
        d.CH375SetBufDownloadEx.restype = u
        d.CH375DriverCommand.argtypes = [u, ctypes.c_void_p]
        d.CH375DriverCommand.restype = u
        d.CH375WriteEndP.argtypes = [u, u, ctypes.c_char_p, p]
        d.CH375WriteEndP.restype = u
        d.CH375ReadEndP.argtypes = [u, u, ctypes.c_char_p, p]
        d.CH375ReadEndP.restype = u
        if not d.CH375OpenDevice(index):
            raise OSError("CH375OpenDevice failed")
        class DriverCommand(ctypes.Structure):
            _pack_ = 1
            _fields_ = [("function", u), ("length", u),
                        ("buffer", ctypes.c_ubyte * 64)]
        io_mode = DriverCommand()
        io_mode.function = 0x12
        io_mode.length = 1
        io_mode.buffer[0] = 0
        if not d.CH375DriverCommand(index, ctypes.byref(io_mode)):
            self.close()
            raise OSError("CH375SetIOMode(sync) failed")
        if not d.CH375SetTimeoutEx(index, 1500, 1500, 1500, 1500):
            self.close()
            raise OSError("CH375SetTimeoutEx failed")
        if not d.CH375SetBufUploadEx(index, 1, 2, 1024 * 1024):
            self.close()
            raise OSError("CH375SetBufUploadEx(EP2) failed")
        if not d.CH375SetBufUploadEx(index, 1, 3, 1024 * 1024):
            self.close()
            raise OSError("CH375SetBufUploadEx(EP3) failed")
        if not d.CH375SetBufDownloadEx(index, 0, 1, 0):
            self.close()
            raise OSError("CH375SetBufDownloadEx(EP1) failed")

    def close(self):
        if self.dll is not None:
            self.dll.CH375CloseDevice(self.index)
            self.dll = None

    def command(self, cmd, payload=b""):
        packet = bytearray(64)
        packet[0] = cmd
        packet[1] = len(payload)
        packet[2:2 + len(payload)] = payload
        length = ctypes.c_ulong(len(packet))
        if not self.dll.CH375WriteEndP(self.index, 1, bytes(packet), ctypes.byref(length)) or length.value != 64:
            raise OSError(f"EP1 write failed ({length.value}/64)")
        for _ in range(10):
            response = ctypes.create_string_buffer(32)
            length = ctypes.c_ulong(32)
            self.dll.CH375ReadEndP(self.index, 1, response, ctypes.byref(length))
            if length.value:
                data = response.raw[:length.value]
                if len(data) < 3 or data[0] != (cmd | 0x10):
                    raise OSError(f"unexpected response: {data.hex(' ')}")
                return data
        raise OSError("no EP1 response")

    def get(self):
        return self.command(CMD_GET_USB3_MUX)

    def set(self, mode):
        return self.command(CMD_SET_USB3_MUX, bytes([mode]))


def show(data):
    if len(data) < 14 or data[1] < 12:
        raise OSError(f"short diagnostic response: {data.hex(' ')}")
    status, version, mode, state = data[2:6]
    u16 = lambda offset: data[offset] | (data[offset + 1] << 8)
    print(f"status={status} payload_version={version} mode={MODE_NAMES.get(mode, mode)}")
    print(f"CC1: raw={u16(6)} mv={u16(10)} active={bool(state & 0x01)} (PA1/ADC1_CH1)")
    print(f"CC2: raw={u16(8)} mv={u16(12)} active={bool(state & 0x02)} (PA0/ADC1_CH0)")
    print(f"ADC_ready={bool(state & 0x04)} EN_N={0 if state & 0x08 else 1} "
          f"SEL={1 if state & 0x10 else 0} orientation_valid={bool(state & 0x20)} "
          f"applied_port={'B' if state & 0x40 else 'A'}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["status", *MODES], default="status", nargs="?")
    args = parser.parse_args()
    dev = Device()
    try:
        if args.action != "status":
            show(dev.set(MODES[args.action]))
            time.sleep(0.4)
        show(dev.get())
    finally:
        dev.close()


if __name__ == "__main__":
    main()
