from django.urls import path

from . import views


app_name = "portal"

urlpatterns = [
    path("", views.home, name="home"),
    path("healthz/", views.healthz, name="healthz"),
    path("csi/", views.csi_home, name="csi_home"),
    path("csi/download/", views.release_download, name="release_download"),
    path("csi/account/signup/", views.signup, name="signup"),
    path("csi/account/login/", views.login_view, name="login"),
    path("csi/account/logout/", views.logout_view, name="logout"),
    path("csi/account/resend/", views.resend_verification, name="resend_verification"),
    path("csi/account/verify/<uidb64>/<token>/", views.verify_email, name="verify_email"),
    path("csi/templates/", views.template_list, name="template_list"),
    path("csi/templates/<uuid:file_id>/download/", views.template_download, name="template_download"),
    path("csi/templates/<uuid:file_id>/delete/", views.template_delete, name="template_delete"),
]
