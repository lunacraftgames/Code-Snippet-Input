import mimetypes
from pathlib import Path

from django.conf import settings
from django.contrib import messages
from django.contrib.auth import login, logout
from django.contrib.auth.decorators import login_required
from django.core.mail import send_mail
from django.http import FileResponse, Http404, JsonResponse
from django.shortcuts import get_object_or_404, redirect, render
from django.template.loader import render_to_string
from django.urls import reverse
from django.utils.encoding import force_bytes, force_str
from django.utils.http import urlsafe_base64_decode, urlsafe_base64_encode
from django.views.decorators.http import require_GET, require_http_methods, require_POST

from .forms import EmailLoginForm, SignUpForm, TemplateUploadForm
from .models import TemplateFile, User
from .tokens import email_verification_token


def _send_verification_email(request, user):
    uid = urlsafe_base64_encode(force_bytes(user.pk))
    token = email_verification_token.make_token(user)
    path = reverse("portal:verify_email", kwargs={"uidb64": uid, "token": token})
    verify_url = f"{settings.SITE_URL}{path}"
    body = render_to_string("portal/email/verify.txt", {"verify_url": verify_url})
    send_mail(
        subject="验证你的 Code Snippet Input 账号",
        message=body,
        from_email=settings.DEFAULT_FROM_EMAIL,
        recipient_list=[user.email],
        fail_silently=False,
    )


@require_GET
def home(request):
    return render(request, "portal/home.html")


@require_GET
def csi_home(request):
    release_path = settings.RELEASE_FILE_PATH
    release_size = release_path.stat().st_size if release_path.is_file() else None
    return render(request, "portal/csi_home.html", {"release_size": release_size})


@require_http_methods(["GET", "POST"])
def signup(request):
    if not settings.REGISTRATION_ENABLED:
        return render(request, "portal/signup_unavailable.html", status=503)
    if request.user.is_authenticated:
        return redirect("portal:template_list")
    form = SignUpForm(request.POST or None)
    if request.method == "POST" and form.is_valid():
        user = form.save()
        try:
            _send_verification_email(request, user)
        except Exception:
            user.delete()
            messages.error(request, "验证邮件发送失败，请稍后重试。")
        else:
            return render(request, "portal/signup_done.html", {"email": user.email})
    return render(request, "portal/signup.html", {"form": form})


@require_http_methods(["GET", "POST"])
def login_view(request):
    if request.user.is_authenticated:
        return redirect("portal:template_list")
    form = EmailLoginForm(request=request, data=request.POST or None)
    if request.method == "POST" and form.is_valid():
        login(request, form.get_user())
        return redirect(request.GET.get("next") or "portal:template_list")
    return render(request, "portal/login.html", {"form": form})


@require_POST
def logout_view(request):
    logout(request)
    return redirect("portal:csi_home")


@require_GET
def verify_email(request, uidb64, token):
    try:
        user_id = force_str(urlsafe_base64_decode(uidb64))
        user = User.objects.get(pk=user_id)
    except (TypeError, ValueError, OverflowError, User.DoesNotExist):
        user = None
    if user is not None and email_verification_token.check_token(user, token):
        user.is_active = True
        user.save(update_fields=["is_active"])
        messages.success(request, "邮箱验证成功，现在可以登录。")
        return redirect("portal:login")
    return render(request, "portal/verification_invalid.html", status=400)


@require_http_methods(["GET", "POST"])
def resend_verification(request):
    if not settings.REGISTRATION_ENABLED:
        return render(request, "portal/signup_unavailable.html", status=503)
    if request.method == "POST":
        email = request.POST.get("email", "").strip().lower()
        user = User.objects.filter(email__iexact=email, is_active=False).first()
        if user:
            try:
                _send_verification_email(request, user)
            except Exception:
                pass
        return render(request, "portal/resend_done.html")
    return render(request, "portal/resend.html")


@require_GET
def release_download(request):
    path = settings.RELEASE_FILE_PATH
    if not path.is_file():
        raise Http404("Release package is not available")
    return FileResponse(path.open("rb"), as_attachment=True, filename=path.name, content_type="application/zip")


@login_required
@require_http_methods(["GET", "POST"])
def template_list(request):
    form = TemplateUploadForm(request.POST or None, request.FILES or None)
    if request.method == "POST" and form.is_valid():
        form.save_for(request.user)
        messages.success(request, "模板已安全上传。")
        return redirect("portal:template_list")
    files = request.user.template_files.all()
    return render(request, "portal/template_list.html", {"form": form, "files": files})


@login_required
@require_GET
def template_download(request, file_id):
    item = get_object_or_404(TemplateFile, pk=file_id, owner=request.user)
    if not item.file.storage.exists(item.file.name):
        raise Http404("File not found")
    content_type = item.content_type or mimetypes.guess_type(item.original_name)[0] or "application/octet-stream"
    return FileResponse(item.file.open("rb"), as_attachment=True, filename=item.original_name, content_type=content_type)


@login_required
@require_POST
def template_delete(request, file_id):
    item = get_object_or_404(TemplateFile, pk=file_id, owner=request.user)
    item.delete()
    messages.success(request, "模板已删除。")
    return redirect("portal:template_list")


@require_GET
def healthz(request):
    return JsonResponse({"status": "ok"})
