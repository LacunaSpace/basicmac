:: !/bin/sh
:: 
::  Copyright (C) 2020, Matthijs Kooijman <matthijs@stdin.nl>
:: 
::  --- Revised 3-Clause BSD License ---
::  Redistribution and use in source and binary forms, with or without modification,
::  are permitted provided that the following conditions are met:
:: 
::      * Redistributions of source code must retain the above copyright notice,
::        this list of conditions and the following disclaimer.
::      * Redistributions in binary form must reproduce the above copyright notice,
::        this list of conditions and the following disclaimer in the documentation
::        and/or other materials provided with the distribution.
::      * Neither the name of the copyright holder nor the names of its contributors
::        may be used to endorse or promote products derived from this software
::        without specific prior written permission.
:: 
::  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
::  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
::  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
::  DISCLAIMED. IN NO EVENT SHALL SEMTECH BE LIABLE FOR ANY DIRECT, INDIRECT,
::  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
::  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
::  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
::  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
::  OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
::  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

:: 
::  Export script for Windows
:: 

@ECHO off > nul

GOTO :MAIN
:USAGE
	echo Usage: %0 TARGET_DIR
	echo Create an Arduino-compatible library in the given directory,
	echo overwriting existing files.
	EXIT /b 0

:MAIN

:: Find full path to script, but strip trailing backslash
SET SRC=%~dp0
IF %SRC:~-1%==\ SET SRC=%SRC:~0,-1%

:: Select operation: "-" is command, else run
SET MAYBE_OPTION=%1
IF "%MAYBE_OPTION%" == "--help" (
	GOTO :USAGE
) ELSE IF "%MAYBE_OPTION%" == "-h" (
	 GOTO :USAGE
) ELSE IF  "%MAYBE_OPTION:~0,1%"=="-" (
	ECHO Unknown option: %MAYBE_OPTION% 1>&2
        EXIT /B 1
)

:: This uses ~ to strip quotes surrounding the argument, if any
SET TARGET=%~1

IF "%TARGET%" == "" (
	GOTO :USAGE
)

IF NOT EXIST "%TARGET%"/.. (
	ECHO Parent of %TARGET% should exist 1>&2
	EXIT /B 1
)

IF EXIST "%TARGET%" (
	SET /P AREYOUSURE=%TARGET% exists, remove before export? [yN]
)

:: Process the result in a separate if, since the entire IF block is expanded
:: before running it, so any variables set inside the IF are only applied
:: *after* the IF.
IF EXIST "%TARGET%" (
	IF /I "%AREYOUSURE%" == "y" (
		rmdir /s /q "%TARGET%"
	)
)

:: This appends \ to all destination directories, which tells XCOPY that when
:: the destination does not create yet, it should be created as a directory
:: (otherwise it asks).
::
:: Note that when the source is a file, it is copied into the target, but when
:: the source is a directory, its contents are copied into the target.
::
:: Note that when using /E (recursive copy), the source should be absolute,
:: otherwise xcopy behaves weirdly.
XCOPY /F /Y "%SRC%\library.properties" "%TARGET%\"
XCOPY /F /Y "%SRC%\basicmac.h" "%TARGET%\src\"
XCOPY /F /Y /E "%SRC%\..\..\lmic" "%TARGET%\src\lmic\"
XCOPY /F /Y /E "%SRC%\hal" "%TARGET%\src\hal\"
XCOPY /F /Y /E "%SRC%\..\..\aes" "%TARGET%\src\aes\"
XCOPY /F /Y /E "%SRC%\examples" "%TARGET%\examples\"
XCOPY /F /Y "%SRC%\board.h" "%TARGET%\src\lmic\"
XCOPY /F /Y "%SRC%\hw.h" "%TARGET%\src\lmic\"

:: Note that %%E is set to just the expanded example directory name, not the
:: full path or path relative to the current directory...
FOR /D %%E in ("%TARGET%/examples/*") DO (
	XCOPY /F /Y /E "%SRC%\examples-common-files" "%TARGET%\examples\%%E\"
)

EXIT /b 0
