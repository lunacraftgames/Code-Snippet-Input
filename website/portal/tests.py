import shutil
import tempfile
from pathlib import Path

from django.contrib.auth.tokens import default_token_generator
from django.core import mail
from django.core.files.uploadedfile import SimpleUploadedFile
from django.test import TestCase, override_settings
from django.urls import reverse
from django.utils.encoding import force_bytes
from django.utils.http import urlsafe_base64_encode

from .models import TemplateFile, User


@override_settings(
    REGISTRATION_ENABLED=True,
    EMAIL_BACKEND="django.core.mail.backends.locmem.EmailBackend",
    SITE_URL="https://lunacraftgames.fyi",
    SECURE_SSL_REDIRECT=False,
)
class PortalTests(TestCase):
    def setUp(self):
        self.media_root = tempfile.mkdtemp()
        self.media_override = override_settings(MEDIA_ROOT=self.media_root)
        self.media_override.enable()

    def tearDown(self):
        self.media_override.disable()
        shutil.rmtree(self.media_root, ignore_errors=True)

    def test_home_has_single_project_link(self):
        response = self.client.get(reverse("portal:home"))
        self.assertEqual(response.status_code, 200)
        self.assertContains(response, "CodeSnippetInput", count=1)
        self.assertContains(response, 'href="/csi/"', count=1)

    def test_signup_creates_inactive_user_and_sends_verification(self):
        response = self.client.post(
            reverse("portal:signup"),
            {"email": "person@example.com", "password1": "Correct-Horse-47!", "password2": "Correct-Horse-47!"},
        )
        self.assertEqual(response.status_code, 200)
        self.assertFalse(User.objects.get(email="person@example.com").is_active)
        self.assertEqual(len(mail.outbox), 1)
        self.assertIn("/csi/account/verify/", mail.outbox[0].body)

    def test_verification_activates_user(self):
        user = User.objects.create_user(email="person@example.com", password="Correct-Horse-47!", is_active=False)
        uid = urlsafe_base64_encode(force_bytes(user.pk))
        token = default_token_generator.make_token(user)
        response = self.client.get(reverse("portal:verify_email", kwargs={"uidb64": uid, "token": token}))
        self.assertRedirects(response, reverse("portal:login"))
        user.refresh_from_db()
        self.assertTrue(user.is_active)

    def test_xml_upload_and_owner_isolation(self):
        owner = User.objects.create_user(email="owner@example.com", password="Correct-Horse-47!")
        other = User.objects.create_user(email="other@example.com", password="Correct-Horse-47!")
        self.client.force_login(owner)
        payload = b'<?xml version="1.0"?><templateSet group="Python"></templateSet>'
        response = self.client.post(
            reverse("portal:template_list"),
            {"display_name": "Python", "file": SimpleUploadedFile("python.xml", payload, content_type="application/xml")},
        )
        self.assertRedirects(response, reverse("portal:template_list"))
        item = TemplateFile.objects.get(owner=owner)
        self.client.force_login(other)
        self.assertEqual(
            self.client.get(reverse("portal:template_download", kwargs={"file_id": item.id})).status_code,
            404,
        )

    def test_rejects_non_template_extension(self):
        owner = User.objects.create_user(email="owner@example.com", password="Correct-Horse-47!")
        self.client.force_login(owner)
        response = self.client.post(
            reverse("portal:template_list"),
            {"display_name": "Bad", "file": SimpleUploadedFile("bad.exe", b"not a template")},
        )
        self.assertEqual(response.status_code, 200)
        self.assertContains(response, "只允许上传")
        self.assertFalse(TemplateFile.objects.exists())
