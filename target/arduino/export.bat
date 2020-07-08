@ECHO off > nul
:: Batch file to install BasicMAC from the git repository in to an
:: existing Arduino installation by specifying the location of the 
:: User library directory e.g.: "C:\Users\UserName\Documents\Arduino\libraries"

:: \name Remko Welling (pe1mew@gmail.com)
:: \date 8-7-2020
:: \version 1.0

:: \todo This script cannot work when whitespaces are in the directory path.
:: As a workaround, export the library else where and move it in to the Arduino library directory.

SET option=%1
SET source_directory=%~dp0
SET target_directory=%2

2>NUL CALL :CASE_%option% 

IF ERRORLEVEL 1 CALL :DEFAULT_CASE

EXIT /B

:CASE_-c
:CASE_--create
   IF EXIST %target_directory% (
      GOTO :COPY
   ) ELSE (
      ECHO [ERROR] Destination directory %target_directory% does not exist. Please correct destination name.
   )
GOTO :EOF

:CASE_-h
:CASE_--help
   ECHO Create an Arduino-compatible library in the given directory, overwriting existing files.
   ECHO -h  --help   This information.
   ECHO -c  --create ^<directory^> Create Arduino library at specified location
GOTO :EOF

:DEFAULT_CASE
   ECHO Unknown command. Type: export.bat -h or --help for options.
GOTO :EOF

:COPY
   :: Test if target directory has a terminating back-slash
   IF NOT "%target_directory:~-1%"=="\" (
      ECHO [ERROR] Target directory has no terminating "\" (back-slash^). Please correct target directory accordingly.
      GOTO :EOF
   )
   :: Copy all files in to the library in the target directory
   ECHO Ceating Arduino library in: %target_directory%
   IF NOT EXIST %target_directory%\BasicMAC (
      md "%target_directory%\BasicMAC"
   )
   IF NOT EXIST %target_directory%\BasicMAC\src (
      md "%target_directory%\BasicMAC\src"
   )
   IF NOT EXIST %target_directory%\BasicMAC\src\lmic (
      md "%target_directory%\BasicMAC\src\lmic"
   )
   IF NOT EXIST %target_directory%\BasicMAC\src\hal (
      md "%target_directory%\BasicMAC\src\hal"
   )
   IF NOT EXIST %target_directory%\BasicMAC\src\aes (
      md "%target_directory%\BasicMAC\src\aes"
   )
   IF NOT EXIST %target_directory%\BasicMAC\examples (
      md "%target_directory%\BasicMAC\examples"
   )
   IF NOT EXIST %target_directory%\BasicMAC\examples\basicmac-abp (
      md "%target_directory%\BasicMAC\examples\basicmac-abp"
   )
   IF NOT EXIST %target_directory%\BasicMAC\examples\basicmac-otaa (
      md "%target_directory%\BasicMAC\examples\basicmac-otaa"
   )
   
   @ECHO on
   COPY "%source_directory%library.properties" "%target_directory%BasicMAC\library.properties"
   COPY "%source_directory%basicmac.h" "%target_directory%BasicMAC\src"
   COPY "%source_directory%..\..\lmic" "%target_directory%BasicMAC\src\lmic"
   COPY "%source_directory%hal" "%target_directory%BasicMAC\src\hal"
   COPY "%source_directory%..\..\aes" "%target_directory%BasicMAC\src\aes"
   COPY "%source_directory%examples\basicmac-otaa" "%target_directory%BasicMAC\examples\basicmac-otaa"
   COPY "%source_directory%examples-common-files" "%target_directory%BasicMAC\examples\basicmac-otaa"
   COPY "%source_directory%examples\basicmac-abp" "%target_directory%BasicMAC\examples\basicmac-abp"
   COPY "%source_directory%examples-common-files" "%target_directory%BasicMAC\examples\basicmac-abp"
   COPY "%source_directory%board.h" "%target_directory%BasicMAC\src\lmic"
   COPY "%source_directory%hw.h" "%target_directory%BasicMAC\src\lmic"
   @ECHO off
GOTO :EOF