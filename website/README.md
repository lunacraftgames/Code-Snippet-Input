# CodeSnippetInput website

Containerized Django site for `lunacraftgames.fyi` and `/csi`.

## Architecture

- Django 5.2 LTS and Gunicorn in the `csi-web` container.
- Existing 1Panel PostgreSQL through the external `1panel-network`.
- Existing host-networked 1Panel OpenResty as the public HTTPS reverse proxy. The application binds only to `127.0.0.1:18080`.
- WhiteNoise for versioned static assets.
- Private uploaded files in `./data`; downloads always pass an ownership check.
- Release package mounted read-only from `./release`.

## Required configuration

Copy `.env.example` to `.env`, generate independent values for the Django secret and database role password, then configure SMTP before setting `REGISTRATION_ENABLED=true`.

The application expects the database role to have `USAGE` and `CREATE` on the `code_snippet_input` schema and to use that schema in its PostgreSQL search path.

## Operations

```sh
docker compose up -d --build
docker compose logs -f web
docker compose exec web python manage.py createsuperuser
```

Run tests without PostgreSQL:

```sh
docker compose run --rm -e DATABASE_ENGINE=sqlite -e DJANGO_SECURE_SSL_REDIRECT=false web python manage.py test
```

Uploaded files are not public media URLs. The application streams them only after confirming that the current user owns the database record.
