import time, socket, requests, math, threading
from io import BytesIO
from PIL import Image, ImageDraw
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

# ================= 核心配置 =================
ESP32_IP = "192.168.100.104"
LAN_IP = "192.168.100.1" 
URL_P = f"http://{LAN_IP}:8124/tiles/players.json"
URL_B = f"http://{LAN_IP}:8124/tiles"

config = {
    "target_player": "", 
    "zoom_level": 32,
    "view_mode": "cave", # 默认优先看洞穴层级
    "all_players": []
}

# ================= Web 管理后台 (9999) =================
class AdminHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        query = parse_qs(urlparse(self.path).query)
        if self.path.startswith("/set"): config["target_player"] = query.get("p", [""])[0]
        elif self.path.startswith("/zoom"): config["zoom_level"] = int(query.get("v", [32])[0])
        elif self.path.startswith("/view"): config["view_mode"] = query.get("m", ["cave"])[0]
        
        self.send_response(200)
        self.send_header("Content-type", "text/html; charset=utf-8")
        self.end_headers()
        
        btn_s = "padding:12px; margin:5px; border:none; border-radius:8px; background:#444; color:white; font-weight:bold; cursor:pointer; min-width:100px;"
        act_z = "background:#ff4444; box-shadow: 0 0 10px #ff4444;"
        act_v = "background:#28a745; box-shadow: 0 0 10px #28a745;"

        p_btns = "".join([f'<a href="/set?p={n}"><button style="{btn_s} {"background:#007bff;" if config["target_player"]==n else ""}">{n}</button></a>' for n in config['all_players']])
        
        # 放大缩小按钮
        zooms = [(16,"32格"),(32,"64格"),(64,"128格"),(128,"256格")]
        z_btns = "".join([f'<a href="/zoom?v={v}"><button style="{btn_s} {act_z if config["zoom_level"]==v else ""}">{t}</button></a>' for v, t in zooms])
        
        html = f"""
        <html><head><meta name="viewport" content="width=device-width, initial-scale=1.0">
        <style>body{{background:#1a1a1a; color:#eee; text-align:center; font-family:sans-serif;}} .card{{background:#2d2d2d; padding:20px; border-radius:15px; display:inline-block; margin-top:10px; border:1px solid #444;}}</style>
        </head><body><div class="card">
            <h3 style="color:#ff4444;">MC RADAR V16.5</h3>
            <div style="margin:10px; padding:10px; background:#222; border-radius:8px;">
                目标: <b>{config['target_player'] or "自动"}</b> | 模式: <b>{"层级" if config['view_mode']=="cave" else "地表"}</b>
            </div>
            <h4>📡 玩家选择</h4><div>{p_btns}</div>
            <h4 style="margin-top:20px;">🗺️ 视图切换</h4>
            <a href="/view?m=surface"><button style="{btn_s} {act_v if config['view_mode']=="surface" else ""}">地表视图</button></a>
            <a href="/view?m=cave"><button style="{btn_s} {act_v if config['view_mode']=="cave" else ""}">层级/洞穴</button></a>
            <h4 style="margin-top:20px;">🔍 放大/缩小 (分辨率)</h4>
            <div style="display:grid; grid-template-columns: 1fr 1fr;">{z_btns}</div>
            <br><a href="/set?p="><button style="{btn_s} background:#666; width:90%;">恢复自动模式</button></a>
        </div></body></html>
        """
        self.wfile.write(html.encode())

threading.Thread(target=lambda: HTTPServer(('0.0.0.0', 9999), AdminHandler).serve_forever(), daemon=True).start()

# ================= 渲染与发送逻辑 =================
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def draw_ui(draw, health, yaw):
    # 红点中心
    draw.point((32, 32), fill=(255, 0, 0))
    # 青色方向点
    angle = math.radians(yaw + 180)
    dx, dy = int(4 * math.sin(angle)), int(-4 * math.cos(angle))
    draw.point((32 + dx, 32 + dy), fill=(0, 255, 255))
    # 3x3 极小红心 (上移到 59 行)
    start_x, start_y = 2, 59
    h_int = int(health)
    for i in range(10):
        x = start_x + (i * 4)
        c = (255,0,0) if (h_int - i*2) >= 2 else (45,0,0)
        draw.point([(x,start_y),(x+2,start_y),(x,start_y+1),(x+1,start_y+1),(x+2,start_y+1),(x+1,start_y+2)], fill=c)

def send_to_esp32(data):
    for i in range(0, len(data), 1024):
        sock.sendto(bytes([i // 1024, 8]) + data[i:i + 1024], (ESP32_IP, 12345))
        time.sleep(0.002)

while True:
    try:
        r = requests.get(URL_P, timeout=2)
        all_p = r.json().get("players", [])
        config["all_players"] = [p['name'] for p in all_p]
        target = next((p for p in all_p if p['name'] == config["target_player"]), None) if config["target_player"] else (all_p[0] if all_p else None)
        
        if target:
            x, z, world, health, yaw = int(target['x']), int(target['z']), target['world'], target.get('health', 20), target.get('yaw', 0)
            tx, tz, zm = x >> 9, z >> 9, config["zoom_level"]
            clean_w = world.split(':')[-1]
            
            img_data = None
            # 优先根据用户选择的模式寻找瓦片
            v_mode = config["view_mode"] # 'cave' 或 'surface'
            path_hints = [f"{v_mode}/0", "surface/0", "flat/0", "0"]
            
            for sub in path_hints:
                url = f"{URL_B}/{clean_w}/{sub}/{tx}_{tz}.png"
                try:
                    res = requests.get(url, timeout=1)
                    if res.status_code == 200 and res.content.startswith(b'\x89PNG'):
                        img_data = res.content
                        break
                except: continue
            
            if img_data:
                raw_img = Image.open(BytesIO(img_data)).convert("RGBA")
                bg = Image.new("RGB", (512, 512), (0, 0, 0))
                bg.paste(raw_img, mask=raw_img.split()[3])
                lx, lz = x - (tx << 9), z - (tz << 9)
                final = bg.crop((lx-zm, lz-zm, lx+zm, lz+zm)).resize((64, 64), Image.NEAREST)
                draw_ui(ImageDraw.Draw(final), health, yaw)
                
                buf = bytearray()
                for rv, gv, bv in list(final.getdata()):
                    val = ((rv & 0xF8) << 8) | ((gv & 0xFC) << 3) | (bv >> 3)
                    buf.extend(val.to_bytes(2, 'little'))
                send_to_esp32(bytes(buf))
    except: pass
    time.sleep(0.4)