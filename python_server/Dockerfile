# 基础环境
FROM python:3.9-slim

# 设置工作目录
WORKDIR /app

# 换源并安装依赖 (opencv-python-headless 是关键，体积小且不报错)
RUN pip install requests numpy opencv-python-headless pillow -i https://pypi.tuna.tsinghua.edu.cn/simple

# 复制脚本
COPY app.py .

# 启动命令
CMD ["python", "-u", "app.py"]