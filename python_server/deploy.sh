#!/bin/bash

# 0. 定位到项目目录 (防止在错误路径执行)
cd /mnt/sata1-1/docker/mc-radar

echo ">>> 1. 开始构建镜像 (强制无缓存)..."
# --no-cache 是关键：防止 Docker 使用旧的缓存层，导致改了代码没生效
docker build --no-cache -t mc-radar-img .

echo ">>> 2. 清理旧容器..."
# -f 强制删除正在运行的容器
docker rm -f mc-radar

echo ">>> 3. 启动新容器..."
# --net=host: 关键参数！让容器共用宿主机网络 IP，
# 否则 UDP 广播发不出去，且 Web 后台(9999端口)无法访问。
# -u: python 的 unbuffered 模式，确保日志实时打印
docker run -d \
  --name mc-radar \
  --net=host \
  --restart=always \
  mc-radar-img \
  python3 -u app.py

echo ">>> 4. 部署完成！正在查看实时日志 (按 Ctrl+C 退出日志)..."
sleep 2
docker logs -f mc-radar