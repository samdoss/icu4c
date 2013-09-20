/*
******************************************************************************
* Copyright (C) 2003-2013, International Business Machines Corporation
* and others. All Rights Reserved.
******************************************************************************
*
* File ISLAMCAL.H
*
* Modification History:
*
*   Date        Name        Description
*   10/14/2003  srl         ported from java IslamicCalendar
*****************************************************************************
*/

#include "islamcal.h"

#if !UCONFIG_NO_FORMATTING

#include "umutex.h"
#include <float.h>
#include "gregoimp.h" // Math
#include "astro.h" // CalendarAstronomer
#include "uhash.h"
#include "ucln_in.h"
#include "uassert.h"

static const UDate HIJRA_MILLIS = -42521587200000.0;    // 7/16/622 AD 00:00

// Debugging
#ifdef U_DEBUG_ISLAMCAL
# include <stdio.h>
# include <stdarg.h>
static void debug_islamcal_loc(const char *f, int32_t l)
{
    fprintf(stderr, "%s:%d: ", f, l);
}

static void debug_islamcal_msg(const char *pat, ...)
{
    va_list ap;
    va_start(ap, pat);
    vfprintf(stderr, pat, ap);
    fflush(stderr);
}
// must use double parens, i.e.:  U_DEBUG_ISLAMCAL_MSG(("four is: %d",4));
#define U_DEBUG_ISLAMCAL_MSG(x) {debug_islamcal_loc(__FILE__,__LINE__);debug_islamcal_msg x;}
#else
#define U_DEBUG_ISLAMCAL_MSG(x)
#endif


// --- The cache --
// cache of months
static UMutex astroLock = U_MUTEX_INITIALIZER;  // pod bay door lock
static icu::CalendarCache *gMonthCache = NULL;
static icu::CalendarAstronomer *gIslamicCalendarAstro = NULL;

U_CDECL_BEGIN
static UBool calendar_islamic_cleanup(void) {
    if (gMonthCache) {
        delete gMonthCache;
        gMonthCache = NULL;
    }
    if (gIslamicCalendarAstro) {
        delete gIslamicCalendarAstro;
        gIslamicCalendarAstro = NULL;
    }
    return TRUE;
}
U_CDECL_END

U_NAMESPACE_BEGIN

// Implementation of the IslamicCalendar class

/**
 * Friday EPOC
 */
static const int32_t CIVIL_EPOC = 1948440;

/**
  * Thursday EPOC
  */
static const int32_t ASTRONOMICAL_EPOC = 1948439;


static const int32_t UMALQURA_YEAR_START = 1318;
static const int32_t UMALQURA_YEAR_END = 1480;

static const int UMALQURA_MONTHLENGTH[] = {
    //* 1318 -1322 */ "0101 0111 0100", "1001 0111 0110", "0100 1011 0111", "0010 0101 0111", "0101 0010 1011",
                            0x0574,           0x0975,           0x06A7,           0x0257,           0x052B,
    //* 1323 -1327 */ "0110 1001 0101", "0110 1100 1010", "1010 1101 0101", "0101 0101 1011", "0010 0101 1101",
                            0x0695,           0x06CA,           0x0AD5,           0x055B,           0x025B,
    //* 1328 -1332 */ "1001 0010 1101", "1100 1001 0101", "1101 0100 1010", "1110 1010 0101", "0110 1101 0010",
                            0x092D,           0x0C95,           0x0D4A,           0x0E5B,           0x025B,
    //* 1333 -1337 */ "1010 1101 0101", "0101 0101 1010", "1010 1010 1011", "0100 0100 1011", "0110 1010 0101",
                            0x0AD5,           0x055A,           0x0AAB,           0x044B,           0x06A5,
    //* 1338 -1342 */ "0111 0101 0010", "1011 1010 1001", "0011 0111 0100", "1010 1011 0110", "0101 0101 0110",
                            0x0752,           0x0BA9,           0x0374,           0x0AB6,           0x0556,
    //* 1343 -1347 */ "1010 1010 1010", "1101 0101 0010", "1101 1010 1001", "0101 1101 0100", "1010 1110 1010",
                            0x0AAA,           0x0D52,           0x0DA9,           0x05D4,           0x0AEA,
    //* 1348 -1352 */ "0100 1101 1101", "0010 0110 1110", "1001 0010 1110", "1010 1010 0110", "1101 0101 0100",
                            0x04DD,           0x026E,           0x092E,           0x0AA6,           0x0D54,
    //* 1353 -1357 */ "0101 1010 1010", "0101 1011 0101", "0010 1011 0100", "1001 0011 0111", "0100 1001 1011",
                            0x05AA,           0x05B5,           0x02B4,           0x0937,           0x049B,
    //* 1358 -1362 */ "1010 0100 1011", "1011 0010 0101", "1011 0101 0100", "1011 0110 1010", "0101 0110 1101",
                            0x0A4B,           0x0B25,           0x0B54,           0x0B6A,           0x056D,
    //* 1363 -1367 */ "0100 1010 1101", "1010 0101 0101", "1101 0010 0101", "1110 1001 0010", "1110 1100 1001",
                            0x04AD,           0x0A55,           0x0D25,           0x0E92,           0x0EC9,
    //* 1368 -1372 */ "0110 1101 0100", "1010 1110 1010", "0101 0110 1011", "0100 1010 1011", "0110 1000 0101",
                            0x06D4,           0x0ADA,           0x056B,           0x04AB,           0x0685,
    //* 1373 -1377 */ "1011 0100 1001", "1011 1010 0100", "1011 1011 0010", "0101 1011 0101", "0010 1011 1010",
                            0x0B49,           0x0BA4,           0x0BB2,           0x05B5,           0x02BA,
    //* 1378 -1382 */ "1001 0101 1011", "0100 1010 1011", "0101 0101 0101", "0110 1011 0010", "0110 1101 1001",
                            0x095B,           0x04AB,           0x0555,           0x06B2,           0x06D9,
    //* 1383 -1387 */ "0010 1110 1100", "1001 0110 1110", "0100 1010 1110", "1010 0101 0110", "1101 0010 1010",
                            0x02EC,           0x096E,           0x04AE,           0x0A56,           0x0D2A,
    //* 1388 -1392 */ "1101 0101 0101", "0101 1010 1010", "1010 1011 0101", "0100 1011 1011", "0000 0101 1011",
                            0x0D55,           0x05AA,           0x0AB5,           0x04BB,           0x005B,
    //* 1393 -1397 */ "1001 0010 1011", "1010 1001 0101", "0011 0100 1010", "1011 1010 0101", "0101 1010 1010",
                            0x092B,           0x0A95,           0x034A,           0x0BA5,           0x05AA,
    //* 1398 -1402 */ "1010 1011 0101", "0101 0101 0110", "1010 1001 0110", "1101 0100 1010", "1110 1010 0101",
                            0x0AB5,           0x0556,           0x0A96,           0x0B4A,           0x0EA5,
    //* 1403 -1407 */ "0111 0101 0010", "0110 1110 1001", "0011 0110 1010", "1010 1010 1101", "0101 0101 0101",
                            0x0752,           0x06E9,           0x036A,           0x0AAD,           0x0555,
    //* 1408 -1412 */ "1010 1010 0101", "1011 0101 0010", "1011 1010 1001", "0101 1011 0100", "1001 1011 1010",
                            0x0AA5,           0x0B52,           0x0BA9,           0x05B4,           0x09BA,
    //* 1413 -1417 */ "0100 1101 1011", "0010 0101 1101", "0101 0010 1101", "1010 1010 0101", "1010 1101 0100",
                            0x04DB,           0x025D,           0x052D,           0x0AA5,           0x0AD4,
    //* 1418 -1422 */ "1010 1110 1010", "0101 0110 1101", "0100 1011 1101", "0010 0011 1101", "1001 0001 1101",
                            0x0AEA,           0x056D,           0x04BD,           0x023D,           0x091D,
    //* 1423 -1427 */ "1010 1001 0101", "1011 0100 1010", "1011 0101 1010", "0101 0110 1101", "0010 1011 0110",
                            0x0A95,           0x0B4A,           0x0B5A,           0x056D,           0x02B6,
    //* 1428 -1432 */ "1001 0011 1011", "0100 1001 1011", "0110 0101 0101", "0110 1010 1001", "0111 0101 0100",
                            0x093B,           0x049B,           0x0655,           0x06A9,           0x0754,
    //* 1433 -1437 */ "1011 0110 1010", "0101 0110 1100", "1010 1010 1101", "0101 0101 0101", "1011 0010 1001",
                            0x0B6A,           0x056C,           0x0AAD,           0x0555,           0x0B29,
    //* 1438 -1442 */ "1011 1001 0010", "1011 1010 1001", "0101 1101 0100", "1010 1101 1010", "0101 0101 1010",
                            0x0B92,           0x0BA9,           0x05D4,           0x0ADA,           0x055A,
    //* 1443 -1447 */ "1010 1010 1011", "0101 1001 0101", "0111 0100 1001", "0111 0110 0100", "1011 1010 1010",
                            0x0AAB,           0x0595,           0x0749,           0x0764,           0x0BAA,
    //* 1448 -1452 */ "0101 1011 0101", "0010 1011 0110", "1010 0101 0110", "1110 0100 1101", "1011 0010 0101",
                            0x05B5,           0x02B6,           0x0A56,           0x0E4D,           0x0B25,
    //* 1453 -1457 */ "1011 0101 0010", "1011 0110 1010", "0101 1010 1101", "0010 1010 1110", "1001 0010 1111",
                            0x0B52,           0x0B6A,           0x05AD,           0x02AE,           0x092F,
    //* 1458 -1462 */ "0100 1001 0111", "0110 0100 1011", "0110 1010 0101", "0110 1010 1100", "1010 1101 0110",
                            0x0497,           0x064B,           0x06A5,           0x06AC,           0x0AD6,
    //* 1463 -1467 */ "0101 0101 1101", "0100 1001 1101", "1010 0100 1101", "1101 0001 0110", "1101 1001 0101",
                            0x055D,           0x049D,           0x0A4D,           0x0D16,           0x0D95,
    //* 1468 -1472 */ "0101 1010 1010", "0101 1011 0101", "0010 1001 1010", "1001 0101 1011", "0100 1010 1100",
                            0x05AA,           0x05B5,           0x029A,           0x095B,           0x04AC,
    //* 1473 -1477 */ "0101 1001 0101", "0110 1100 1010", "0110 1110 0100", "1010 1110 1010", "0100 1111 0101",
                            0x0595,           0x06CA,           0x06E4,           0x0AEA,           0x04F5,
    //* 1478 -1480 */ "0010 1011 0110", "1001 0101 0110", "1010 1010 1010"
                            0x02B6,           0x0956,           0x0AAA
};

int32_t getUmalqura_MonthLength(int32_t y, int32_t m) {
    int32_t mask = (int32_t) (0x01 << (11 - m));    // set mask for bit corresponding to month
    if((UMALQURA_MONTHLENGTH[y] & mask) == 0 )
        return 29;
    else
        return 30;

}

//-------------------------------------------------------------------------
// Constructors...
//-------------------------------------------------------------------------

const char *IslamicCalendar::getType() const { 
    if(civil==CIVIL) {
        return "islamic-civil";
    } else if(civil==ASTRONOMICAL){
        return "islamic";
    } else if(civil==TBLA){
        return "islamic-tbla";
    } else if(civil==UMALQURA){
      return "islamic-umalqura";
    } else {
      U_ASSERT(false); // out of range
      return "islamic-unknown";
    }
}

Calendar* IslamicCalendar::clone() const {
    return new IslamicCalendar(*this);
}

IslamicCalendar::IslamicCalendar(const Locale& aLocale, UErrorCode& success, ECivil beCivil)
:   Calendar(TimeZone::createDefault(), aLocale, success),
civil(beCivil)
{
    setTimeInMillis(getNow(), success); // Call this again now that the vtable is set up properly.
}

IslamicCalendar::IslamicCalendar(const IslamicCalendar& other) : Calendar(other), civil(other.civil) {
}

IslamicCalendar::~IslamicCalendar()
{
}

/**
* Determines whether this object uses the fixed-cycle Islamic civil calendar
* or an approximation of the religious, astronomical calendar.
*
* @param beCivil   <code>true</code> to use the civil calendar,
*                  <code>false</code> to use the astronomical calendar.
* @draft ICU 2.4
*/
void IslamicCalendar::setCivil(ECivil beCivil, UErrorCode &status)
{
    if (civil != beCivil) {
        // The fields of the calendar will become invalid, because the calendar
        // rules are different
        UDate m = getTimeInMillis(status);
        civil = beCivil;
        clear();
        setTimeInMillis(m, status);
    }
}

/**
* Returns <code>true</code> if this object is using the fixed-cycle civil
* calendar, or <code>false</code> if using the religious, astronomical
* calendar.
* @draft ICU 2.4
*/
UBool IslamicCalendar::isCivil() {
    return (civil == CIVIL);
}

//-------------------------------------------------------------------------
// Minimum / Maximum access functions
//-------------------------------------------------------------------------

// Note: Current IslamicCalendar implementation does not work
// well with negative years.

// TODO: In some cases the current ICU Islamic calendar implementation shows
// a month as having 31 days. Since date parsing now uses range checks based
// on the table below, we need to change the range for last day of month to
// include 31 as a workaround until the implementation is fixed.
static const int32_t LIMITS[UCAL_FIELD_COUNT][4] = {
    // Minimum  Greatest    Least  Maximum
    //           Minimum  Maximum
    {        0,        0,        0,        0}, // ERA
    {        1,        1,  5000000,  5000000}, // YEAR
    {        0,        0,       11,       11}, // MONTH
    {        1,        1,       50,       51}, // WEEK_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // WEEK_OF_MONTH
    {        1,        1,       29,       31}, // DAY_OF_MONTH - 31 to workaround for cal implementation bug, should be 30
    {        1,        1,      354,      355}, // DAY_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DAY_OF_WEEK
    {       -1,       -1,        5,        5}, // DAY_OF_WEEK_IN_MONTH
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // AM_PM
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // HOUR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // HOUR_OF_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MINUTE
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // SECOND
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MILLISECOND
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // ZONE_OFFSET
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DST_OFFSET
    {        1,        1,  5000000,  5000000}, // YEAR_WOY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DOW_LOCAL
    {        1,        1,  5000000,  5000000}, // EXTENDED_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // JULIAN_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MILLISECONDS_IN_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // IS_LEAP_MONTH
};

/**
* @draft ICU 2.4
*/
int32_t IslamicCalendar::handleGetLimit(UCalendarDateFields field, ELimitType limitType) const {
    return LIMITS[field][limitType];
}

//-------------------------------------------------------------------------
// Assorted calculation utilities
//

/**
* Determine whether a year is a leap year in the Islamic civil calendar
*/
UBool IslamicCalendar::civilLeapYear(int32_t year)
{
    return (14 + 11 * year) % 30 < 11;
}

/**
* Return the day # on which the given year starts.  Days are counted
* from the Hijri epoch, origin 0.
*/
int32_t IslamicCalendar::yearStart(int32_t year) const{
    if (civil == CIVIL || civil == TBLA ||
        (civil == UMALQURA && year < UMALQURA_YEAR_START)) 
    {
        return (year-1)*354 + ClockMath::floorDivide((3+11*year),30);
    } else if(civil==ASTRONOMICAL){
        return trueMonthStart(12*(year-1));
    } else {
        int32_t ys = yearStart(UMALQURA_YEAR_START-1);
        ys+= handleGetYearLength(UMALQURA_YEAR_START-1);
        for(int i=UMALQURA_YEAR_START; i< year; i++){  
            ys+= handleGetYearLength(i);
        }
        return ys;
    }
}

/**
* Return the day # on which the given month starts.  Days are counted
* from the Hijri epoch, origin 0.
*
* @param year  The hijri year
* @param year  The hijri month, 0-based
*/
int32_t IslamicCalendar::monthStart(int32_t year, int32_t month) const {
    if (civil == CIVIL || civil == TBLA) {
        return (int32_t)uprv_ceil(29.5*month)
            + (year-1)*354 + (int32_t)ClockMath::floorDivide((3+11*year),30);
    } else if(civil==ASTRONOMICAL){
        return trueMonthStart(12*(year-1) + month);
    } else {
        int32_t ms = yearStart(year);
        for(int i=0; i< month; i++){
            ms+= handleGetMonthLength(year, i);
        }
        return ms;
    }
}

/**
* Find the day number on which a particular month of the true/lunar
* Islamic calendar starts.
*
* @param month The month in question, origin 0 from the Hijri epoch
*
* @return The day number on which the given month starts.
*/
int32_t IslamicCalendar::trueMonthStart(int32_t month) const
{
    UErrorCode status = U_ZERO_ERROR;
    int32_t start = CalendarCache::get(&gMonthCache, month, status);

    if (start==0) {
        // Make a guess at when the month started, using the average length
        UDate origin = HIJRA_MILLIS 
            + uprv_floor(month * CalendarAstronomer::SYNODIC_MONTH) * kOneDay;

        // moonAge will fail due to memory allocation error
        double age = moonAge(origin, status);
        if (U_FAILURE(status)) {
            goto trueMonthStartEnd;
        }

        if (age >= 0) {
            // The month has already started
            do {
                origin -= kOneDay;
                age = moonAge(origin, status);
                if (U_FAILURE(status)) {
                    goto trueMonthStartEnd;
                }
            } while (age >= 0);
        }
        else {
            // Preceding month has not ended yet.
            do {
                origin += kOneDay;
                age = moonAge(origin, status);
                if (U_FAILURE(status)) {
                    goto trueMonthStartEnd;
                }
            } while (age < 0);
        }
        start = (int32_t)ClockMath::floorDivide((origin - HIJRA_MILLIS), (double)kOneDay) + 1;
        CalendarCache::put(&gMonthCache, month, start, status);
    }
trueMonthStartEnd :
    if(U_FAILURE(status)) {
        start = 0;
    }
    return start;
}

/**
* Return the "age" of the moon at the given time; this is the difference
* in ecliptic latitude between the moon and the sun.  This method simply
* calls CalendarAstronomer.moonAge, converts to degrees, 
* and adjusts the result to be in the range [-180, 180].
*
* @param time  The time at which the moon's age is desired,
*              in millis since 1/1/1970.
*/
double IslamicCalendar::moonAge(UDate time, UErrorCode &status)
{
    double age = 0;

    umtx_lock(&astroLock);
    if(gIslamicCalendarAstro == NULL) {
        gIslamicCalendarAstro = new CalendarAstronomer();
        if (gIslamicCalendarAstro == NULL) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return age;
        }
        ucln_i18n_registerCleanup(UCLN_I18N_ISLAMIC_CALENDAR, calendar_islamic_cleanup);
    }
    gIslamicCalendarAstro->setTime(time);
    age = gIslamicCalendarAstro->getMoonAge();
    umtx_unlock(&astroLock);

    // Convert to degrees and normalize...
    age = age * 180 / CalendarAstronomer::PI;
    if (age > 180) {
        age = age - 360;
    }

    return age;
}

//----------------------------------------------------------------------
// Calendar framework
//----------------------------------------------------------------------

/**
* Return the length (in days) of the given month.
*
* @param year  The hijri year
* @param year  The hijri month, 0-based
* @draft ICU 2.4
*/
int32_t IslamicCalendar::handleGetMonthLength(int32_t extendedYear, int32_t month) const {

    int32_t length = 0;

    if (civil == CIVIL || civil == TBLA ||
        (civil == UMALQURA && (extendedYear<UMALQURA_YEAR_START || extendedYear>UMALQURA_YEAR_END)) ) {
        length = 29 + (month+1) % 2;
        if (month == DHU_AL_HIJJAH && civilLeapYear(extendedYear)) {
            length++;
        }
    } else if(civil == ASTRONOMICAL){
        month = 12*(extendedYear-1) + month;
        length =  trueMonthStart(month+1) - trueMonthStart(month) ;
    } else {
        length = getUmalqura_MonthLength(extendedYear - UMALQURA_YEAR_START, month);
    }
    return length;
}

/**
* Return the number of days in the given Islamic year
* @draft ICU 2.4
*/
int32_t IslamicCalendar::handleGetYearLength(int32_t extendedYear) const {
    if (civil == CIVIL || civil == TBLA ||
        (civil == UMALQURA && (extendedYear<UMALQURA_YEAR_START || extendedYear>UMALQURA_YEAR_END)) ) {
        return 354 + (civilLeapYear(extendedYear) ? 1 : 0);
    } else if(civil == ASTRONOMICAL){
        int32_t month = 12*(extendedYear-1);
        return (trueMonthStart(month + 12) - trueMonthStart(month));
    } else {
        int len = 0;
        for(int i=0; i<12; i++)
            len += handleGetMonthLength(extendedYear, i);
        return len;
    }
}

//-------------------------------------------------------------------------
// Functions for converting from field values to milliseconds....
//-------------------------------------------------------------------------

// Return JD of start of given month/year
/**
* @draft ICU 2.4
*/
int32_t IslamicCalendar::handleComputeMonthStart(int32_t eyear, int32_t month, UBool /* useMonth */) const {
    return monthStart(eyear, month) + 1948439;
}    

//-------------------------------------------------------------------------
// Functions for converting from milliseconds to field values
//-------------------------------------------------------------------------

/**
* @draft ICU 2.4
*/
int32_t IslamicCalendar::handleGetExtendedYear() {
    int32_t year;
    if (newerField(UCAL_EXTENDED_YEAR, UCAL_YEAR) == UCAL_EXTENDED_YEAR) {
        year = internalGet(UCAL_EXTENDED_YEAR, 1); // Default to year 1
    } else {
        year = internalGet(UCAL_YEAR, 1); // Default to year 1
    }
    return year;
}

/**
* Override Calendar to compute several fields specific to the Islamic
* calendar system.  These are:
*
* <ul><li>ERA
* <li>YEAR
* <li>MONTH
* <li>DAY_OF_MONTH
* <li>DAY_OF_YEAR
* <li>EXTENDED_YEAR</ul>
* 
* The DAY_OF_WEEK and DOW_LOCAL fields are already set when this
* method is called. The getGregorianXxx() methods return Gregorian
* calendar equivalents for the given Julian day.
* @draft ICU 2.4
*/
void IslamicCalendar::handleComputeFields(int32_t julianDay, UErrorCode &status) {
    int32_t year, month, dayOfMonth, dayOfYear;
    UDate startDate;
    int32_t days = julianDay - CIVIL_EPOC;

    if (civil == CIVIL || civil == TBLA) {
        if(civil == TBLA)
            days = julianDay - ASTRONOMICAL_EPOC;
        // Use the civil calendar approximation, which is just arithmetic
        year  = (int)ClockMath::floorDivide( (double)(30 * days + 10646) , 10631.0 );
        month = (int32_t)uprv_ceil((days - 29 - yearStart(year)) / 29.5 );
        month = month<11?month:11;
        startDate = monthStart(year, month);
    } else if(civil == ASTRONOMICAL){
        // Guess at the number of elapsed full months since the epoch
        int32_t months = (int32_t)uprv_floor((double)days / CalendarAstronomer::SYNODIC_MONTH);

        startDate = uprv_floor(months * CalendarAstronomer::SYNODIC_MONTH);

        double age = moonAge(internalGetTime(), status);
        if (U_FAILURE(status)) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        if ( days - startDate >= 25 && age > 0) {
            // If we're near the end of the month, assume next month and search backwards
            months++;
        }

        // Find out the last time that the new moon was actually visible at this longitude
        // This returns midnight the night that the moon was visible at sunset.
        while ((startDate = trueMonthStart(months)) > days) {
            // If it was after the date in question, back up a month and try again
            months--;
        }

        year = months / 12 + 1;
        month = months % 12;
    } else if(civil == UMALQURA) {
        int32_t umalquraStartdays = yearStart(UMALQURA_YEAR_START) ;
        if( days < umalquraStartdays){
                //Use Civil calculation
                year  = (int)ClockMath::floorDivide( (double)(30 * days + 10646) , 10631.0 );
                month = (int32_t)uprv_ceil((days - 29 - yearStart(year)) / 29.5 );
                month = month<11?month:11;
                startDate = monthStart(year, month);
            }else{
                int y =UMALQURA_YEAR_START-1, m =0;
                long d = 1;
                while(d > 0){ 
                    y++; 
                    d = days - yearStart(y) +1;
                    if(d == handleGetYearLength(y)){
                        m=11;
                        break;
                    }else if(d < handleGetYearLength(y) ){
                        int monthLen = handleGetMonthLength(y, m); 
                        m=0;
                        while(d > monthLen){
                            d -= monthLen;
                            m++;
                            monthLen = handleGetMonthLength(y, m);
                        }
                        break;
                    }
                }
                year = y;
                month = m;
            }
    } else { // invalid 'civil'
      U_ASSERT(false); // should not get here, out of range
      year=month=0;
    }

    dayOfMonth = (days - monthStart(year, month)) + 1;

    // Now figure out the day of the year.
    dayOfYear = (days - monthStart(year, 0) + 1);


    internalSet(UCAL_ERA, 0);
    internalSet(UCAL_YEAR, year);
    internalSet(UCAL_EXTENDED_YEAR, year);
    internalSet(UCAL_MONTH, month);
    internalSet(UCAL_DAY_OF_MONTH, dayOfMonth);
    internalSet(UCAL_DAY_OF_YEAR, dayOfYear);       
}    

UBool
IslamicCalendar::inDaylightTime(UErrorCode& status) const
{
    // copied from GregorianCalendar
    if (U_FAILURE(status) || (&(getTimeZone()) == NULL && !getTimeZone().useDaylightTime())) 
        return FALSE;

    // Force an update of the state of the Calendar.
    ((IslamicCalendar*)this)->complete(status); // cast away const

    return (UBool)(U_SUCCESS(status) ? (internalGet(UCAL_DST_OFFSET) != 0) : FALSE);
}

/**
 * The system maintains a static default century start date and Year.  They are
 * initialized the first time they are used.  Once the system default century date 
 * and year are set, they do not change.
 */
static UDate           gSystemDefaultCenturyStart       = DBL_MIN;
static int32_t         gSystemDefaultCenturyStartYear   = -1;
static icu::UInitOnce  gSystemDefaultCenturyInit        = U_INITONCE_INITIALIZER;


UBool IslamicCalendar::haveDefaultCentury() const
{
    return TRUE;
}

UDate IslamicCalendar::defaultCenturyStart() const
{
    // lazy-evaluate systemDefaultCenturyStart
    umtx_initOnce(gSystemDefaultCenturyInit, &initializeSystemDefaultCentury);
    return gSystemDefaultCenturyStart;
}

int32_t IslamicCalendar::defaultCenturyStartYear() const
{
    // lazy-evaluate systemDefaultCenturyStartYear
    umtx_initOnce(gSystemDefaultCenturyInit, &initializeSystemDefaultCentury);
    return gSystemDefaultCenturyStartYear;
}


void U_CALLCONV
IslamicCalendar::initializeSystemDefaultCentury()
{
    // initialize systemDefaultCentury and systemDefaultCenturyYear based
    // on the current time.  They'll be set to 80 years before
    // the current time.
    UErrorCode status = U_ZERO_ERROR;
    IslamicCalendar calendar(Locale("@calendar=islamic-civil"),status);
    if (U_SUCCESS(status)) {
        calendar.setTime(Calendar::getNow(), status);
        calendar.add(UCAL_YEAR, -80, status);

        gSystemDefaultCenturyStart = calendar.getTime(status);
        gSystemDefaultCenturyStartYear = calendar.get(UCAL_YEAR, status);
    }
    // We have no recourse upon failure unless we want to propagate the failure
    // out.
}



UOBJECT_DEFINE_RTTI_IMPLEMENTATION(IslamicCalendar)

U_NAMESPACE_END

#endif

