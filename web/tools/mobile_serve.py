#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
手机移动端访问服务器（Phase 1：局域网直连）

作用：
  - 从「仓库根」起静态服务（保持 app.js 里 `/cases/...`、`/赛题/原件/...` 等绝对路径可用）
  - 自动探测本机内网 IPv4，打印手机可直接访问的 URL
  - 在终端画出二维码（手机扫码直达，免手输 IP）；无 qrcode 库时自动降级为纯文字提示

用法：
  python web/tools/mobile_serve.py            # 默认端口 8765
  python web/tools/mobile_serve.py --port 9000

前提：
  手机与电脑连同一个 WiFi/路由器；首次可能需在 Windows 防火墙放行 Python。
"""

import argparse
import http.server
import os
import socket
import sys
from functools import partial

# 仓库根 = 本文件所在目录(web/tools) 的上两级
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
APP_PATH = "/web/"  # 网页入口（相对仓库根）


def detect_lan_ip() -> str:
    """探测本机内网 IPv4。用 UDP 连一个外部地址（不真正发包）来拿到出口网卡 IP。"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("223.5.5.5", 80))  # 阿里 DNS，仅用于选路，不产生实际流量
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip


def print_qr(url: str) -> None:
    """终端打印二维码；没装 qrcode 库就降级为提示。"""
    try:
        import qrcode  # type: ignore

        qr = qrcode.QRCode(border=1)
        qr.add_data(url)
        qr.make(fit=True)
        qr.print_ascii(invert=True)
    except ImportError:
        print("  (未安装 qrcode 库，无法显示二维码)")
        print("  想要扫码可执行： pip install qrcode")
        print(f"  或手机浏览器直接输入上面的地址")


def main() -> int:
    parser = argparse.ArgumentParser(description="电赛刷题网页 · 手机访问服务器")
    parser.add_argument("--port", type=int, default=8765, help="监听端口，默认 8765")
    args = parser.parse_args()

    os.chdir(REPO_ROOT)
    ip = detect_lan_ip()
    mobile_url = f"http://{ip}:{args.port}{APP_PATH}"
    local_url = f"http://localhost:{args.port}{APP_PATH}"

    print()
    print("=" * 54)
    print("  电赛备赛刷题网页 · 手机访问模式")
    print(f"  服务根：{REPO_ROOT}")
    print("-" * 54)
    print(f"  本机访问： {local_url}")
    print(f"  手机访问： {mobile_url}")
    print("=" * 54)
    print("  用手机扫下面的二维码（需与电脑同一 WiFi）：")
    print()
    print_qr(mobile_url)
    print()
    print("  关闭此窗口即停止服务；首次手机连不上请放行 Windows 防火墙。")
    print("=" * 54)
    print()

    # 绑定 0.0.0.0，局域网内其它设备才可达
    handler = partial(http.server.SimpleHTTPRequestHandler, directory=REPO_ROOT)
    try:
        httpd = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), handler)
    except OSError as e:
        print(f"[错误] 端口 {args.port} 启动失败：{e}")
        print("       换个端口重试： python web/tools/mobile_serve.py --port 9000")
        return 1

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止服务。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
