﻿
/*
Copyright (c) 2011-2014 Maximus5
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define HIDE_USE_EXCEPTION_INFO
#define SHOWDEBUGSTR

#include "Header.h"
#include "Match.h"
#include "VirtualConsole.h"

CMatch::CMatch()
	:m_Type(etr_None)
	,mn_Row(-1), mn_Col(-1)
	,mn_MatchLeft(-1), mn_MatchRight(-1)
	,mn_Start(-1), mn_End(-1)
	,mn_SrcLength(-1)
	,mn_SrcFrom(-1)
{
	ms_Protocol[0] = 0;
}

CMatch::~CMatch()
{
}

// Returns the length of matched string
int CMatch::Match(ExpandTextRangeType etr, LPCWSTR asLine/*This may be NOT 0-terminated*/, int anLineLen/*Length of buffer*/, int anFrom/*Cursor pos*/)
{
	m_Type = etr_None;
	ms_Match.Empty();
	mn_Row = mn_Col = -1;
	ms_Protocol[0] = 0;
	mn_MatchLeft = mn_MatchRight = -1;
	mn_Start = mn_End = -1;

	if (!asLine || !*asLine || (anFrom < 0) || (anLineLen <= anFrom))
		return 0;

	if (!m_SrcLine.Set(asLine, anLineLen))
    	return 0;
	mn_SrcFrom = anFrom;
	mn_SrcLength = anLineLen;

	int iRc = 0;

	if (etr == etr_Word)
	{
		if (MatchWord(m_SrcLine.ms_Arg, anLineLen, anFrom, mn_MatchLeft, mn_MatchRight))
		{
			_ASSERTE(mn_MatchRight >= mn_MatchLeft);
			iRc = (mn_MatchRight - mn_MatchLeft + 1);
			m_Type = etr_Word;
		}
	}
	else if (etr == etr_AnyClickable)
	{
		if (MatchAny())
		{
			_ASSERTE(mn_MatchRight >= mn_MatchLeft);
			_ASSERTE(m_Type != etr_None);
			iRc = (mn_MatchRight - mn_MatchLeft + 1);
		}
	}
	else
	{
		_ASSERTE(FALSE && "Unsupported etr argument value!");
	}

	return iRc;
}


/* ****************************************** */

bool CMatch::IsFileLineTerminator(LPCWSTR pChar, LPCWSTR pszTermint)
{
	// Расчитано на закрывающие : или ) или ] или ,
	if (wcschr(pszTermint, *pChar))
		return true;
	// Script.ps1:35 знак:23
	if (*pChar == L' ')
	{
		// few chars, colon, digits
		for (int i = 1; i < 20; i++)
		{
			if (pChar[i] == 0 || !isAlpha(pChar[i])) //wcschr(L" \t\xA0", pChar[i]))
				break;
			if (pChar[i+1] == L':')
			{
				if (isDigit(pChar[i+2]))
				{
					for (int j = i+3; j < 25; j++)
					{
						if (isDigit(pChar[j]))
							continue;
						if (isSpace(pChar[j]))
							return true;
					}
				}
				break;
			}
		}
	}
	return false;
}

void CMatch::StoreMatchText(LPCWSTR asPrefix, LPCWSTR pszTrimRight)
{
	int iMailTo = asPrefix ? lstrlen(asPrefix/*L"mailto:"*/) : 0;
	INT_PTR cchTextMax = (mn_MatchRight - mn_MatchLeft + 1 + iMailTo);

	wchar_t* pszText = ms_Match.GetBuffer(cchTextMax);
	if (!pszText)
	{
		return; // Недостаточно памяти под текст?
	}

	if (iMailTo)
	{
		// Добавить префикс протокола
		lstrcpyn(pszText, asPrefix/*L"mailto:"*/, iMailTo+1);
		pszText += iMailTo;
		cchTextMax -= iMailTo;
	}

	if (pszTrimRight)
	{
		while ((mn_MatchRight > mn_MatchLeft) && wcschr(pszTrimRight, m_SrcLine.ms_Arg[mn_MatchRight]))
			mn_MatchRight--;
	}

	// Return hyperlink target
	memmove(pszText, m_SrcLine.ms_Arg+mn_MatchLeft, (mn_MatchRight - mn_MatchLeft + 1)*sizeof(*pszText));
	pszText[mn_MatchRight - mn_MatchLeft + 1] = 0;
}

bool CMatch::FindRangeStart(int& crFrom/*[In/Out]*/, int& crTo/*[In/Out]*/, bool& bUrlMode, LPCWSTR pszBreak, LPCWSTR pszUrlDelim, LPCWSTR pszSpacing, LPCWSTR pszUrl, LPCWSTR pszProtocol, LPCWSTR pChar, int nLen)
{
	bool lbRc = false;

	WARNING("Тут пока работаем в экранных координатах");

	// Курсор над комментарием?
	// Попробуем найти начало имени файла
	// 131026 Allows '?', otherwise links like http://go.com/fwlink/?LinkID=1 may fails
	while ((crFrom) > 0 && (pChar[crFrom-1]==L'?' || !wcschr(bUrlMode ? pszUrlDelim : pszBreak, pChar[crFrom-1])))
	{
		if (!bUrlMode && pChar[crFrom] == L'/')
		{
			if ((crFrom >= 2) && ((crFrom + 1) < nLen)
				&& ((pChar[crFrom+1] == L'/') && (pChar[crFrom-1] == L':')
					&& wcschr(pszUrl, pChar[crFrom-2]))) // как минимум одна буква на протокол
			{
				crFrom++;
			}

			if ((crFrom >= 3)
				&& ((pChar[crFrom-1] == L'/') // как минимум одна буква на протокол
					&& (((pChar[crFrom-2] == L':') && wcschr(pszUrl, pChar[crFrom-3])) // http://www.ya.ru
						|| ((crFrom >= 4) && (pChar[crFrom-2] == L'/') && (pChar[crFrom-3] == L':') && wcschr(pszUrl, pChar[crFrom-4])) // file:///c:\file.html
					))
				)
			{
				bUrlMode = true;
				crTo = crFrom-2;
				crFrom -= 3;
				while ((crFrom > 0) && wcschr(pszProtocol, pChar[crFrom-1]))
					crFrom--;
				break;
			}
			else if ((pChar[crFrom] == L'/') && (crFrom >= 1) && (pChar[crFrom-1] == L'/'))
			{
				crFrom++;
				break; // Комментарий в строке?
			}
		}
		crFrom--;
		if (pChar[crFrom] == L':')
		{
			if (pChar[crFrom+1] == L' ')
			{
				// ASM - подсвечивать нужно "test.asasm(1,1)"
				// object.Exception@assembler.d(1239): test.asasm(1,1):
				crFrom += 2;
				break;
			}
			else if (bUrlMode && pChar[crFrom+1] != L'\\' && pChar[crFrom+1] != L'/')
			{
				goto wrap; // Не оно
			}
		}
	}
	while (((crFrom+1) < nLen) && wcschr(pszSpacing, pChar[crFrom]))
		crFrom++;
	if (crFrom > crTo)
	{
		goto wrap; // Fail?
	}

	lbRc = true;
wrap:
	return lbRc;
}

bool CMatch::CheckValidUrl(int& crFrom/*[In/Out]*/, int& crTo/*[In/Out]*/, bool& bUrlMode, LPCWSTR pszUrlDelim, LPCWSTR pszUrl, LPCWSTR pszProtocol, LPCWSTR pChar, int nLen)
{
	bool lbRc = false;

	WARNING("Тут пока работаем в экранных координатах");

	// URL? (Курсор мог стоять над протоколом)
	while ((crTo < nLen) && wcschr(pszProtocol, pChar[crTo]))
		crTo++;
	if (((crTo+1) < nLen) && (pChar[crTo] == L':'))
	{
		if (((crTo+4) < nLen) && (pChar[crTo+1] == L'/') && (pChar[crTo+2] == L'/'))
		{
			bUrlMode = true;
			if (wcschr(pszUrl+2 /*пропустить ":/"*/, pChar[crTo+3])
				|| ((((crTo+5) < nLen) && (pChar[crTo+3] == L'/'))
					&& wcschr(pszUrl+2 /*пропустить ":/"*/, pChar[crTo+4]))
				)
			{
				crFrom = crTo;
				while ((crFrom > 0) && wcschr(pszProtocol, pChar[crFrom-1]))
					crFrom--;
			}
			else
			{
				goto wrap; // Fail
			}
		}
	}

	lbRc = true;
wrap:
	return lbRc;
}

bool CMatch::MatchWord(LPCWSTR asLine/*This may be NOT 0-terminated*/, int anLineLen/*Length of buffer*/, int anFrom/*Cursor pos*/, int& rnStart, int& rnEnd)
{
	rnStart = rnEnd = anFrom;

	TODO("Проверить на ошибки после добавления горизонтальной прокрутки");

	if (!asLine || !*asLine || (anFrom < 0) || (anLineLen <= anFrom))
		return false;

	while ((rnStart > 0)
		&& !(CVirtualConsole::isCharSpace(asLine[rnStart-1]) || CVirtualConsole::isCharNonSpacing(asLine[rnStart-1])))
	{
		rnStart--;
	}

	while (((rnEnd+1) < anLineLen)
		&& !(CVirtualConsole::isCharSpace(asLine[rnEnd]) || CVirtualConsole::isCharNonSpacing(asLine[rnEnd])))
	{
		rnEnd++;
	}

	StoreMatchText(NULL, NULL);

	return true;
}

bool CMatch::MatchAny()
{
	bool bFound = false;

	// В именах файлов недопустимы: "/\:|*?<>~t~r~n
	const wchar_t  pszBreak[] = {
						/*недопустимые в FS*/
						L'\"', '|', '*', '?', '<', '>', '\t', '\r', '\n',
						/*для простоты - учитываем и рамки*/
						ucArrowUp, ucArrowDown, ucDnScroll, ucUpScroll,
						ucBox100, ucBox75, ucBox50, ucBox25,
						ucBoxDblVert, ucBoxSinglVert, ucBoxDblVertSinglRight, ucBoxDblVertSinglLeft,
						ucBoxDblDownRight, ucBoxDblDownLeft, ucBoxDblUpRight,
						ucBoxDblUpLeft, ucBoxSinglDownRight, ucBoxSinglDownLeft, ucBoxSinglUpRight,
						ucBoxSinglUpLeft, ucBoxSinglDownDblHorz, ucBoxSinglUpDblHorz, ucBoxDblDownDblHorz,
						ucBoxDblUpDblHorz, ucBoxSinglDownHorz, ucBoxSinglUpHorz, ucBoxDblDownSinglHorz,
						ucBoxDblUpSinglHorz, ucBoxDblVertRight, ucBoxDblVertLeft,
						ucBoxSinglVertRight, ucBoxSinglVertLeft, ucBoxDblVertHorz,
						0};
	const wchar_t* pszSpacing = L" \t\xB7\x2192"; //B7 - режим "Show white spaces", 2192 - символ табуляции там же
	const wchar_t* pszSeparat = L" \t:(";
	const wchar_t* pszTermint = L":)],";
	const wchar_t* pszDigits  = L"0123456789";
	const wchar_t* pszSlashes = L"/\\";
	const wchar_t* pszUrl = L":/\\:%#ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz;?@&=+$,-_.!~*'()0123456789";
	const wchar_t* pszUrlTrimRight = L".,;";
	const wchar_t* pszProtocol = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.";
	const wchar_t* pszEMail = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.";
	const wchar_t* pszUrlDelim = L"\\\"<>{}[]^`' \t\r\n";
	const wchar_t* pszUrlFileDelim = L"\"<>^ \t\r\n";
	int nColons = 0;
	bool bUrlMode = false, bMaybeMail = false;
	SHORT MailX = -1;
	bool bDigits = false, bLineNumberFound = false, bWasSeparator = false;
	int iExtFound = 0, iBracket = 0;

	mn_MatchLeft = mn_MatchRight = mn_SrcFrom;

	// Курсор над комментарием?
	// Попробуем найти начало имени файла
	if (!FindRangeStart(mn_MatchLeft, mn_MatchRight, bUrlMode, pszBreak, pszUrlDelim, pszSpacing, pszUrl, pszProtocol, m_SrcLine.ms_Arg, mn_SrcLength))
		goto wrap;

	// URL? (Курсор мог стоять над протоколом)
	if (!CheckValidUrl(mn_MatchLeft, mn_MatchRight, bUrlMode, pszUrlDelim, pszUrl, pszProtocol, m_SrcLine.ms_Arg, mn_SrcLength))
		goto wrap;


	// Чтобы корректно флаги обработались (типа наличие расширения и т.п.)
	mn_MatchRight = mn_MatchLeft;

	// Теперь - найти конец.
	// Считаем, что для файлов конец это двоеточие, после которого идет описание ошибки
	// Для протоколов (http/...) - первый недопустимый символ

	TODO("Можно бы и просто открытие файлов прикрутить, без требования 'строки с ошибкой'");

	// -- VC
	// 1>c:\sources\conemu\realconsole.cpp(8104) : error C2065: 'qqq' : undeclared identifier
	// DefResolve.cpp(18) : error C2065: 'sdgagasdhsahd' : undeclared identifier
	// DefResolve.cpp(18): warning: note xxx
	// -- GCC
	// ConEmuC.cpp:49: error: 'qqq' does not name a type
	// 1.c:3: some message
	// file.cpp:29:29: error
	// CPP Check
	// [common\PipeServer.h:1145]: (style) C-style pointer casting
	// Delphi
	// c:\sources\FarLib\FarCtrl.pas(1002) Error: Undeclared identifier: 'PCTL_GETPLUGININFO'
	// FPC
	// FarCtrl.pas(1002,49) Error: Identifier not found "PCTL_GETPLUGININFO"
	// PowerShell
	// Script.ps1:35 знак:23
	// -- Possible?
	// abc.py (3): some message
	// ASM - подсвечивать нужно "test.asasm(1,1)"
	// object.Exception@assembler.d(1239): test.asasm(1,1):
	// Issue 1594
	// /src/class.c:123:m_func(...)
	// /src/class.c:123: m_func(...)

	// -- URL's
	// file://c:\temp\qqq.html
	// http://www.farmanager.com
	// $ http://www.KKK.ru - левее слеша - не срабатывает
	// C:\ConEmu>http://www.KKK.ru - ...
	// -- False detects
	// 29.11.2011 18:31:47
	// C:\VC\unicode_far\macro.cpp  1251 Ln 5951/8291 Col 51 Ch 39 0043h 13:54
	// InfoW1900->SettingsControl(sc.Handle, SCTL_FREE, 0, 0);

	// Нас на интересуют строки типа "11.05.2010 10:20:35"
	// В имени файла должна быть хотя бы одна буква (расширение), причем английская
	// Поехали
	if (bUrlMode)
	{
		LPCWSTR pszDelim = (wcsncmp(m_SrcLine.ms_Arg+mn_MatchLeft, L"file://", 7) == 0) ? pszUrlFileDelim : pszUrlDelim;
		while (((mn_MatchRight+1) < mn_SrcLength) && !wcschr(pszDelim, m_SrcLine.ms_Arg[mn_MatchRight+1]))
			mn_MatchRight++;
	}
	else while ((mn_MatchRight+1) < mn_SrcLength)
	{
		if ((m_SrcLine.ms_Arg[mn_MatchRight] == L'/') && ((mn_MatchRight+1) < mn_SrcLength) && (m_SrcLine.ms_Arg[mn_MatchRight+1] == L'/')
			&& !((mn_MatchRight > 1) && (m_SrcLine.ms_Arg[mn_MatchRight] == L':'))) // и НЕ URL адрес
		{
			goto wrap; // Не оно (комментарий в строке)
		}

		if (bWasSeparator
			&& ((m_SrcLine.ms_Arg[mn_MatchRight] >= L'0' && m_SrcLine.ms_Arg[mn_MatchRight] <= L'9')
				|| (bDigits && (m_SrcLine.ms_Arg[mn_MatchRight] == L',')))) // FarCtrl.pas(1002,49) Error:
		{
			if (bLineNumberFound)
			{
				// gcc такие строки тоже может выкинуть
				// file.cpp:29:29: error
				mn_MatchRight--;
				break;
			}
			if (!bDigits && (mn_MatchLeft < mn_MatchRight) /*&& (m_SrcLine.ms_Arg[mn_MatchRight-1] == L':')*/)
			{
				bDigits = true;
			}
		}
		else
		{
			if (iExtFound != 2)
			{
				if (!iExtFound)
				{
					if (m_SrcLine.ms_Arg[mn_MatchRight] == L'.')
						iExtFound = 1;
				}
				else
				{
					// Не особо заморачиваясь с точками и прочим. Просто небольшая страховка от ложных срабатываний...
					if ((m_SrcLine.ms_Arg[mn_MatchRight] >= L'a' && m_SrcLine.ms_Arg[mn_MatchRight] <= L'z') || (m_SrcLine.ms_Arg[mn_MatchRight] >= L'A' && m_SrcLine.ms_Arg[mn_MatchRight] <= L'Z'))
					{
						iExtFound = 2;
						iBracket = 0;
					}
				}
			}

			if (iExtFound == 2)
			{
				if (m_SrcLine.ms_Arg[mn_MatchRight] == L'.')
				{
					iExtFound = 1;
					iBracket = 0;
				}
				else if (wcschr(pszSlashes, m_SrcLine.ms_Arg[mn_MatchRight]) != NULL)
				{
					// Был слеш, значит расширения - еще нет
					iExtFound = 0;
					iBracket = 0;
					bWasSeparator = false;
				}
				else if (wcschr(pszSpacing, m_SrcLine.ms_Arg[mn_MatchRight]) && wcschr(pszSpacing, m_SrcLine.ms_Arg[mn_MatchRight+1]))
				{
					// Слишком много пробелов
					iExtFound = 0;
					iBracket = 0;
					bWasSeparator = false;
				}
				else
					bWasSeparator = (wcschr(pszSeparat, m_SrcLine.ms_Arg[mn_MatchRight]) != NULL);
			}

			// Расчитано на закрывающие : или ) или ] или ,
			_ASSERTE(pszTermint[0]==L':' && pszTermint[1]==L')' && pszTermint[2]==L']' && pszTermint[3]==L',' && pszTermint[5]==0);
			// Script.ps1:35 знак:23
			if (bDigits && IsFileLineTerminator(m_SrcLine.ms_Arg+mn_MatchRight, pszTermint))
			{
				// Validation
				if (((m_SrcLine.ms_Arg[mn_MatchRight] == L':' || m_SrcLine.ms_Arg[mn_MatchRight] == L' ')
						// Issue 1594: /src/class.c:123:m_func(...)
						/* && (wcschr(pszSpacing, m_SrcLine.ms_Arg[mn_MatchRight+1])
							|| wcschr(pszDigits, m_SrcLine.ms_Arg[mn_MatchRight+1]))*/)
				// Если номер строки обрамлен скобками - скобки должны быть сбалансированы
				|| ((m_SrcLine.ms_Arg[mn_MatchRight] == L')') && (iBracket == 1)
						&& ((m_SrcLine.ms_Arg[mn_MatchRight+1] == L':')
							|| wcschr(pszSpacing, m_SrcLine.ms_Arg[mn_MatchRight+1])
							|| wcschr(pszDigits, m_SrcLine.ms_Arg[mn_MatchRight+1])))
				// [file.cpp:1234]: (cppcheck)
				|| ((m_SrcLine.ms_Arg[mn_MatchRight] == L']') && (m_SrcLine.ms_Arg[mn_MatchRight+1] == L':'))
					)
				{
					_ASSERTE(bLineNumberFound==false);
					bLineNumberFound = true;
					break; // found?
				}
			}
			bDigits = false;

			switch (m_SrcLine.ms_Arg[mn_MatchRight])
			{
			// Пока регулярок нет...
			case L'(': iBracket++; break;
			case L')': iBracket--; break;
			case L'/': case L'\\': iBracket = 0; break;
			case L'@':
				if (MailX != -1)
				{
					bMaybeMail = false;
				}
				else if (((mn_MatchRight > 0) && wcschr(pszEMail, m_SrcLine.ms_Arg[mn_MatchRight-1]))
					&& (((mn_MatchRight+1) < mn_SrcLength) && wcschr(pszEMail, m_SrcLine.ms_Arg[mn_MatchRight+1])))
				{
					bMaybeMail = true;
					MailX = mn_MatchRight;
				}
				break;
			}

			if (m_SrcLine.ms_Arg[mn_MatchRight] == L':')
				nColons++;
			else if (m_SrcLine.ms_Arg[mn_MatchRight] == L'\\' || m_SrcLine.ms_Arg[mn_MatchRight] == L'/')
				nColons = 0;
		}

		if (nColons >= 2)
			break;

		mn_MatchRight++;
		if (wcschr(bUrlMode ? pszUrlDelim : pszBreak, m_SrcLine.ms_Arg[mn_MatchRight]))
		{
			if (bMaybeMail)
				break;
			goto wrap; // Не оно
		}
	} // end of 'while ((mn_MatchRight+1) < mn_SrcLength)'

	if (bUrlMode)
	{
		// Считаем, что OK
		bMaybeMail = false;
	}
	else
	{
		if (!bLineNumberFound && bDigits)
			bLineNumberFound = true;

		if (bLineNumberFound)
			bMaybeMail = false;

		if ((m_SrcLine.ms_Arg[mn_MatchRight] != L':'
				&& m_SrcLine.ms_Arg[mn_MatchRight] != L' '
				&& !((m_SrcLine.ms_Arg[mn_MatchRight] == L')') && iBracket == 1)
				&& !((m_SrcLine.ms_Arg[mn_MatchRight] == L']') && (m_SrcLine.ms_Arg[mn_MatchRight+1] == L':'))
			)
			|| !bLineNumberFound || (nColons > 2))
		{
			if (!bMaybeMail)
				goto wrap;
		}
		if (bMaybeMail || (!bMaybeMail && m_SrcLine.ms_Arg[mn_MatchRight] != L')'))
			mn_MatchRight--;
		// Откатить ненужные пробелы
		while ((mn_MatchLeft < mn_MatchRight) && wcschr(pszSpacing, m_SrcLine.ms_Arg[mn_MatchLeft]))
			mn_MatchLeft++;
		while ((mn_MatchRight > mn_MatchLeft) && wcschr(pszSpacing, m_SrcLine.ms_Arg[mn_MatchRight]))
			mn_MatchRight--;
		if ((mn_MatchLeft + 4) > mn_MatchRight) // 1.c:1: //-V112
		{
			// Слишком коротко, считаем что не оно
			goto wrap;
		}
		if (!bMaybeMail)
		{
			// Проверить, чтобы был в наличии номер строки
			if (!(m_SrcLine.ms_Arg[mn_MatchRight] >= L'0' && m_SrcLine.ms_Arg[mn_MatchRight] <= L'9') // ConEmuC.cpp:49:
				&& !(m_SrcLine.ms_Arg[mn_MatchRight] == L')' && (m_SrcLine.ms_Arg[mn_MatchRight-1] >= L'0' && m_SrcLine.ms_Arg[mn_MatchRight-1] <= L'9'))) // ConEmuC.cpp(49) :
			{
				goto wrap; // Номера строки нет
			}
			// [file.cpp:1234]: (cppcheck) ?
			if ((m_SrcLine.ms_Arg[mn_MatchRight+1] == L']') && (m_SrcLine.ms_Arg[mn_MatchLeft] == L'['))
				mn_MatchLeft++;
			// Чтобы даты ошибочно не подсвечивать:
			// 29.11.2011 18:31:47
			{
				bool bNoDigits = false;
				for (int i = mn_MatchLeft; i <= mn_MatchRight; i++)
				{
					if (m_SrcLine.ms_Arg[i] < L'0' || m_SrcLine.ms_Arg[i] > L'9')
					{
						bNoDigits = true;
					}
				}
				if (!bNoDigits)
					goto wrap;
			}
			// -- уже включены // Для красивости в VC включить скобки
			//if ((m_SrcLine.ms_Arg[mn_MatchRight] == L')') && (m_SrcLine.ms_Arg[mn_MatchRight+1] == L':'))
			//	mn_MatchRight++;
		}
		else // bMaybeMail
		{
			// Для мейлов - проверяем допустимые символы (чтобы пробелов не было и прочего мусора)
			int x = MailX - 1; _ASSERTE(x>=0);
			while ((x > 0) && wcschr(pszEMail, m_SrcLine.ms_Arg[x-1]))
				x--;
			mn_MatchLeft = x;
			x = MailX + 1; _ASSERTE(x<mn_SrcLength);
			while (((x+1) < mn_SrcLength) && wcschr(pszEMail, m_SrcLine.ms_Arg[x+1]))
				x++;
			mn_MatchRight = x;
		}
	} // end "else / if (bUrlMode)"

	// Check mouse pos, it must be inside region
	if ((mn_SrcFrom < mn_MatchLeft) || (mn_MatchRight < mn_SrcFrom))
	{
		goto wrap;
	}

	if (bUrlMode)
		m_Type = etr_Url;
	else
		m_Type = (bLineNumberFound ? etr_FileRow : etr_File);

	// Ok
	if (mn_MatchRight >= mn_MatchLeft)
	{
		bFound = true;

		_ASSERTE(!bMaybeMail || !bUrlMode); // Одновременно - флаги не могут быть выставлены!
		LPCWSTR pszPrefix = (bMaybeMail && !bUrlMode) ? L"mailto:" : NULL;
		if (bMaybeMail && !bUrlMode)
			bUrlMode = true;

		StoreMatchText(pszPrefix, bUrlMode ? pszUrlTrimRight : NULL);

		#ifdef _DEBUG
		if (!bUrlMode && wcsstr(ms_Match.ms_Arg, L"//")!=NULL)
		{
			_ASSERTE(FALSE);
		}
		#endif
	}

wrap:
	return bFound;
}
