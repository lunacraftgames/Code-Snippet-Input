FROM python:3.13-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1

WORKDIR /app

RUN groupadd --gid 10001 app \
    && useradd --uid 10001 --gid app --home-dir /app --shell /usr/sbin/nologin app

COPY requirements.txt ./
RUN pip install --upgrade pip && pip install -r requirements.txt

COPY . .
RUN chmod +x /app/entrypoint.sh \
    && mkdir -p /app/staticfiles /app/data/uploads /app/release \
    && chown -R app:app /app

USER app

RUN DJANGO_SECRET_KEY=build-only-secret \
    DATABASE_HOST=localhost \
    python manage.py collectstatic --noinput

EXPOSE 8000

ENTRYPOINT ["/app/entrypoint.sh"]
