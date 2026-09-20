from django.conf import settings


def site_settings(request):
    return {
        "registration_enabled": settings.REGISTRATION_ENABLED,
        "buy_me_a_coffee_url": settings.BUY_ME_A_COFFEE_URL,
    }
