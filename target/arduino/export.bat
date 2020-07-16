@ECHO off > nul
:: Batch file to install BasicMAC from the git repository in to a
:: Arduino installation by specifying the location of the User 
:: library directory e.g.: "C:\Users\UserName\Documents\Arduino\libraries".

:: Options are: 
::  -h  --help    Help and information.

:: \name Remko Welling (pe1mew@gmail.com)

SET option=%1
SET source_directory=%~dp0

:: Select operation: "-" is command, else run
IF  "%option:~0,1%"=="-" (
   GOTO :CASE
) ELSE (
   GOTO :RUN
)
GOTO :EOF

:CASE
:: Command handeling

2>NUL CALL :CASE_%option% 

IF ERRORLEVEL 1 CALL :DEFAULT_CASE

EXIT /B

:CASE_-h
:CASE_--help
   ECHO Create an Arduino-compatible library in the given directory, overwriting existing files.
   ECHO -h  --help    This information.
GOTO :EOF

:DEFAULT_CASE
   ECHO Unknown command. Type: export.bat -h or --help for options.
GOTO :EOF

:RUN
:: Normal operation.
   IF NOT EXIST "%option:"=%" (
      ECHO Target directory "%option:"=%" does not exist.
      GOTO :EOF
   ) 
   IF EXIST "%option:"=%\BasicMAC" (
      GOTO :DO_REMOVE
   ) ELSE (
      GOTO :DO_CREATE
   )
GOTO :EOF

:DO_REMOVE
:: Remove existing directory, prompt for acknowledge.
   ECHO Removing directory "%option:"=%\BasicMAC",
   SET /P AREYOUSURE=Are you sure (Y/[N])?
   IF /I "%AREYOUSURE%" NEQ "Y" GOTO :EOF
   
   rmdir /s /q "%option:"=%\BasicMAC"

:DO_CREATE
:: Create directory structure for library in target directory
   ECHO Ceating Arduino library in: "%option:"=%"
   IF NOT EXIST "%option:"=%\BasicMAC" (
      md "%option:"=%\BasicMAC"
   )
   IF NOT EXIST "%option:"=%\BasicMAC\src" (
      md "%option:"=%\BasicMAC\src"
   )
   IF NOT EXIST "%option:"=%\BasicMAC\src\lmic" (
      md "%option:"=%\BasicMAC\src\lmic"
   )
   IF NOT EXIST "%option:"=%\BasicMAC\src\hal" (
      md "%option:"=%\BasicMAC\src\hal"
   )
   IF NOT EXIST "%option:"=%\BasicMAC\src\aes" (
      md "%option:"=%\BasicMAC\src\aes"
   )
   IF NOT EXIST "%option:"=%\BasicMAC\examples" (
      md "%option:"=%\BasicMAC\examples"
   )
   IF NOT EXIST "%option:"=%\BasicMAC\examples\basicmac-abp" (
      md "%option:"=%\BasicMAC\examples\basicmac-abp"
   )
   IF NOT EXIST "%option:"=%\BasicMAC\examples\basicmac-otaa" (
      md "%option:"=%\BasicMAC\examples\basicmac-otaa"
   )

:DO_COPY
   :: Copy relevant files from git repository to Arduino Library
   @ECHO on
   COPY "%source_directory%library.properties" "%option:"=%\BasicMAC\library.properties"
   COPY "%source_directory%basicmac.h" "%option:"=%\BasicMAC\src"
   COPY "%source_directory%..\..\lmic" "%option:"=%\BasicMAC\src\lmic"
   COPY "%source_directory%hal" "%option:"=%\BasicMAC\src\hal"
   COPY "%source_directory%..\..\aes" "%option:"=%\BasicMAC\src\aes"
   COPY "%source_directory%examples\basicmac-otaa" "%option:"=%\BasicMAC\examples\basicmac-otaa"
   COPY "%source_directory%examples-common-files" "%option:"=%\BasicMAC\examples\basicmac-otaa"
   COPY "%source_directory%examples\basicmac-abp" "%option:"=%\BasicMAC\examples\basicmac-abp"
   COPY "%source_directory%examples-common-files" "%option:"=%\BasicMAC\examples\basicmac-abp"
   COPY "%source_directory%board.h" "%option:"=%\BasicMAC\src\lmic"
   COPY "%source_directory%hw.h" "%option:"=%\BasicMAC\src\lmic"
   @ECHO off
GOTO :EOF