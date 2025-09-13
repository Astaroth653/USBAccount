@echo off
REM -------------------------------
REM Batch pour créer le service USBKeyService
REM -------------------------------

SET SERVICE_NAME=USBKeyService
SET SERVICE_EXE=C:\USBAccount\USBAccountService.exe

REM Vérifie que le fichier existe
IF NOT EXIST "%SERVICE_EXE%" (
    echo Le fichier %SERVICE_EXE% est introuvable !
    pause
    exit /b 1
)

REM Création du service
sc create %SERVICE_NAME% binPath= "%SERVICE_EXE%" start= auto
IF %ERRORLEVEL% EQU 0 (
    echo Service %SERVICE_NAME% créé avec succès.
) ELSE (
    echo Erreur lors de la création du service.
)

pause
