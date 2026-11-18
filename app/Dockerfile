FROM python:3.10-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY clickhouseinit.sql .
RUN chmod 666 /app/clickhouseinit.sql
COPY main.py .
RUN apt-get update && apt-get install -y libpq-dev gcc
RUN apt-get install net-tools iputils-ping curl -y
# RUN python3 main.py
# CMD ["sh","-c", "env | sort && python main.py"]
CMD ["python","-u", "main.py"]