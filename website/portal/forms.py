import hashlib
import zipfile
from pathlib import Path

from defusedxml import ElementTree as SafeElementTree
from django import forms
from django.conf import settings
from django.contrib.auth import authenticate, password_validation
from django.core.exceptions import ValidationError

from .models import TemplateFile, User


class SignUpForm(forms.Form):
    email = forms.EmailField(label="邮箱", max_length=254)
    password1 = forms.CharField(label="密码", widget=forms.PasswordInput)
    password2 = forms.CharField(label="确认密码", widget=forms.PasswordInput)

    def clean_email(self):
        email = self.cleaned_data["email"].strip().lower()
        if User.objects.filter(email__iexact=email).exists():
            raise ValidationError("该邮箱已经注册。")
        return email

    def clean(self):
        cleaned = super().clean()
        password1 = cleaned.get("password1")
        password2 = cleaned.get("password2")
        if password1 and password2 and password1 != password2:
            self.add_error("password2", "两次输入的密码不一致。")
        if password1:
            candidate = User(email=cleaned.get("email", ""))
            try:
                password_validation.validate_password(password1, candidate)
            except ValidationError as error:
                self.add_error("password1", error)
        return cleaned

    def save(self):
        return User.objects.create_user(
            email=self.cleaned_data["email"],
            password=self.cleaned_data["password1"],
            is_active=False,
        )


class EmailLoginForm(forms.Form):
    email = forms.EmailField(label="邮箱")
    password = forms.CharField(label="密码", widget=forms.PasswordInput)

    def __init__(self, request=None, *args, **kwargs):
        self.request = request
        self.user_cache = None
        super().__init__(*args, **kwargs)

    def clean(self):
        cleaned = super().clean()
        email = cleaned.get("email", "").strip().lower()
        password = cleaned.get("password")
        if email and password:
            self.user_cache = authenticate(self.request, email=email, password=password)
            if self.user_cache is None:
                inactive = User.objects.filter(email__iexact=email, is_active=False).exists()
                message = "请先通过验证邮件激活账号。" if inactive else "邮箱或密码不正确。"
                raise ValidationError(message)
        return cleaned

    def get_user(self):
        return self.user_cache


class TemplateUploadForm(forms.ModelForm):
    class Meta:
        model = TemplateFile
        fields = ["display_name", "file"]
        labels = {"display_name": "模板名称", "file": "XML 或 ZIP 文件"}
        widgets = {"file": forms.ClearableFileInput(attrs={"accept": ".xml,.zip"})}

    def clean_file(self):
        uploaded = self.cleaned_data["file"]
        suffix = Path(uploaded.name).suffix.lower()
        if suffix not in {".xml", ".zip"}:
            raise ValidationError("只允许上传 .xml 或 .zip 文件。")
        if uploaded.size > settings.MAX_UPLOAD_BYTES:
            limit_mb = settings.MAX_UPLOAD_BYTES // (1024 * 1024)
            raise ValidationError(f"文件不能超过 {limit_mb} MB。")

        try:
            if suffix == ".xml":
                SafeElementTree.parse(uploaded)
            else:
                with zipfile.ZipFile(uploaded) as archive:
                    members = [item for item in archive.infolist() if not item.is_dir()]
                    if not members or len(members) > 100:
                        raise ValidationError("ZIP 必须包含 1–100 个 XML 文件。")
                    if any(Path(item.filename).suffix.lower() != ".xml" for item in members):
                        raise ValidationError("ZIP 中只能包含 XML 文件。")
                    if sum(item.file_size for item in members) > 25 * 1024 * 1024:
                        raise ValidationError("ZIP 解压后的内容不能超过 25 MB。")
                    bad_entry = archive.testzip()
                    if bad_entry:
                        raise ValidationError("ZIP 文件损坏。")
        except (zipfile.BadZipFile, SafeElementTree.ParseError):
            raise ValidationError("文件格式无效或内容损坏。")
        finally:
            uploaded.seek(0)
        return uploaded

    def save_for(self, owner):
        item = super().save(commit=False)
        uploaded = self.cleaned_data["file"]
        digest = hashlib.sha256()
        for chunk in uploaded.chunks():
            digest.update(chunk)
        uploaded.seek(0)
        item.owner = owner
        item.original_name = Path(uploaded.name).name[:255]
        item.content_type = getattr(uploaded, "content_type", "")[:100]
        item.size = uploaded.size
        item.sha256 = digest.hexdigest()
        item.save()
        return item
