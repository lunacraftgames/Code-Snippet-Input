from django.contrib import admin
from django.contrib.auth.admin import UserAdmin

from .models import TemplateFile, User


@admin.register(User)
class EmailUserAdmin(UserAdmin):
    ordering = ["email"]
    list_display = ["email", "is_active", "is_staff", "date_joined"]
    search_fields = ["email"]
    fieldsets = (
        (None, {"fields": ("email", "password")}),
        ("Permissions", {"fields": ("is_active", "is_staff", "is_superuser", "groups", "user_permissions")}),
        ("Dates", {"fields": ("last_login", "date_joined")}),
    )
    add_fieldsets = ((None, {"classes": ("wide",), "fields": ("email", "password1", "password2")}),)


@admin.register(TemplateFile)
class TemplateFileAdmin(admin.ModelAdmin):
    list_display = ["display_name", "owner", "original_name", "size", "created_at"]
    search_fields = ["display_name", "original_name", "owner__email", "sha256"]
    readonly_fields = ["id", "size", "sha256", "created_at", "updated_at"]
