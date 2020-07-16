@ECHO off > nul
:: Batch file to install BasicMAC from the git repository in to a
:: Arduino installation by specifying the location of the User 
:: library directory e.g.: "C:\Users\UserName\Documents\Arduino\libraries".

:: Options are: 
::  -h  --help    Help and information.
::  -c  --create  ^<directory^> Create Arduino library at specified location.
::                              When a directory is found with equal name the 
::                              user is informed and the script is aborted.
::  -r  --replace ^<directory^> Replace Arduino library at specified location erasing existing files.
::                              When a directory is found with equal name the 
::                              user is requested to confirm deletion of the directory.

:: \name Remko Welling (pe1mew@gmail.com)

SET option=%1
SET source_directory=%~dp0
SET target_directory=%2

2>NUL CALL :CASE_%option% 

IF ERRORLEVEL 1 CALL :DEFAULT_CASE

EXIT /B

:CASE_-r
:CASE_--replace
:: remove existing folder structure and copy new library at specified location.
   IF EXIST "%target_directory:"=%\BasicMAC" (
      GOTO :DO_REMOVE
   ) ELSE (
      ECHO [ERROR] Destination directory "%target_directory:"=%\BasicMAC" does not exist. Please correct destination name.
   )
GOTO :EOF

:CASE_-c
:CASE_--create
:: create the directory and copy files to it, aborting if it already exists.
   IF EXIST "%target_directory:"=%\BasicMAC" (
      ECHO [ERROR] Destination directory "%target_directory:"=%\BasicMAC" does exist. Please correct destination name.
   ) ELSE (
      GOTO :DO_CREATE
   )
GOTO :EOF

:CASE_-h
:CASE_--help
   ECHO Create an Arduino-compatible library in the given directory, overwriting existing files.
   ECHO -h  --help    This information.
   ECHO -c  --create  ^<directory^> Create Arduino library at specified location.
   ECHO -r  --replace ^<directory^> Replace Arduino library at specified location erasing existing files.
GOTO :EOF

:DEFAULT_CASE
   ECHO Unknown command or abort. Type: export.bat -h or --help for options.
GOTO :EOF

:DO_REMOVE
:: remove existing directory, prompt for acknowledge.
   ECHO Removing directory "%target_directory:"=%\BasicMAC",
   SET /P AREYOUSURE=Are you sure (Y/[N])?
   IF /I "%AREYOUSURE%" NEQ "Y" GOTO :EOF
   
   rmdir /s /q "%target_directory:"=%\BasicMAC"

:DO_CREATE
:: Copy all files in to the library in the target directory
   ECHO Ceating Arduino library in: "%target_directory:"=%"
   IF NOT EXIST "%target_directory:"=%\BasicMAC" (
      md "%target_directory:"=%\BasicMAC"
   )
   IF NOT EXIST "%target_directory:"=%\BasicMAC\src" (
      md "%target_directory:"=%\BasicMAC\src"
   )
   IF NOT EXIST "%target_directory:"=%\BasicMAC\src\lmic" (
      md "%target_directory:"=%\BasicMAC\src\lmic"
   )
   IF NOT EXIST "%target_directory:"=%\BasicMAC\src\hal" (
      md "%target_directory:"=%\BasicMAC\src\hal"
   )
   IF NOT EXIST "%target_directory:"=%\BasicMAC\src\aes" (
      md "%target_directory:"=%\BasicMAC\src\aes"
   )
   IF NOT EXIST "%target_directory:"=%\BasicMAC\examples" (
      md "%target_directory:"=%\BasicMAC\examples"
   )
   IF NOT EXIST "%target_directory:"=%\BasicMAC\examples\basicmac-abp" (
      md "%target_directory:"=%\BasicMAC\examples\basicmac-abp"
   )
   IF NOT EXIST "%target_directory:"=%\BasicMAC\examples\basicmac-otaa" (
      md "%target_directory:"=%\BasicMAC\examples\basicmac-otaa"
   )

:DO_COPY
   :: Copy relevant files from git repository to Arduino Library
   @ECHO on
   COPY "%source_directory%library.properties" "%target_directory:"=%\BasicMAC\library.properties"
   COPY "%source_directory%basicmac.h" "%target_directory:"=%\BasicMAC\src"
   COPY "%source_directory%..\..\lmic" "%target_directory:"=%\BasicMAC\src\lmic"
   COPY "%source_directory%hal" "%target_directory:"=%\BasicMAC\src\hal"
   COPY "%source_directory%..\..\aes" "%target_directory:"=%\BasicMAC\src\aes"
   COPY "%source_directory%examples\basicmac-otaa" "%target_directory:"=%\BasicMAC\examples\basicmac-otaa"
   COPY "%source_directory%examples-common-files" "%target_directory:"=%\BasicMAC\examples\basicmac-otaa"
   COPY "%source_directory%examples\basicmac-abp" "%target_directory:"=%\BasicMAC\examples\basicmac-abp"
   COPY "%source_directory%examples-common-files" "%target_directory:"=%\BasicMAC\examples\basicmac-abp"
   COPY "%source_directory%board.h" "%target_directory:"=%\BasicMAC\src\lmic"
   COPY "%source_directory%hw.h" "%target_directory:"=%\BasicMAC\src\lmic"
   @ECHO off
GOTO :EOF