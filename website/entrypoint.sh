#!/bin/sh
set -eu

if [ "$#" -gt 0 ]; then
  exec "$@"
fi

python manage.py migrate --noinput
python manage.py check --deploy

exec gunicorn config.wsgi:application \
  --bind 0.0.0.0:8000 \
  --workers "${GUNICORN_WORKERS:-3}" \
  --threads "${GUNICORN_THREADS:-2}" \
  --timeout 90 \
  --access-logfile - \
  --error-logfile -
