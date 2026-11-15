#ifndef __GridTextFormat_h__
#define __GridTextFormat_h__

// ETextFormat and the predicate behind both the std::ios::binary flag and the per-slot format filter.
// Out of GridSourceBase.h because that pulls in AGridSource.h, which needs MFC.

namespace OG2 /* OG = OpenGrid */
{
    enum ETextFormat
    {
        etfDefault          = -1,
        etfPlainText        = 0,
        etfQuoteDelimited   = 1,
        etfTabDelimitedText = 2,
        etfXmlElem          = 3,
        // Slot 4 held etfXmlAttr, retired long ago and deleted. Reusing the number keeps the enum
        // contiguous 0-10 and the "row index IS the format" invariant intact.
        etfJson             = 4,
        etfHtml             = 5,
        etfPLSQL1           = 6,
        etfPLSQL2           = 7,
        etfPLSQL3           = 8,
        etfPLSQL4           = 9,
        // XE1: the first format whose bytes are not text - a worksheet is a zip, so its stream is
        // opened binary and the text-only consumers, the clipboard and "Open in Editor", refuse it.
        etfXlsx             = 10,
    };

    // XE1: true of the formats that are not text. Their std::ofstream has to be opened with
    // std::ios::binary, or Windows turns every 0x0A byte into CRLF and the file will not open.
    constexpr bool IsBinaryExportFormat (int format)
    {
        return etfXlsx == format;
    }
}

#endif // __GridTextFormat_h__
