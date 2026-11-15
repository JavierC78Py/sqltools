#include "Keychain.h"
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace {
CFStringRef cf (const QString& s) { const QByteArray u = s.toUtf8(); return CFStringCreateWithBytes(nullptr, (const UInt8*)u.constData(), u.size(), kCFStringEncodingUTF8, false); }

// Consulta base: clase "contraseña genérica" + servicio + cuenta
CFMutableDictionaryRef baseQuery (const QString& service, const QString& account)
{
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(q, kSecClass, kSecClassGenericPassword);
    CFStringRef s = cf(service), a = cf(account);
    CFDictionaryAddValue(q, kSecAttrService, s); CFDictionaryAddValue(q, kSecAttrAccount, a);
    CFRelease(s); CFRelease(a);
    return q;
}
}

bool Keychain::set (const QString& service, const QString& account, const QString& secret)
{
    const QByteArray bytes = secret.toUtf8();
    CFDataRef data = CFDataCreate(nullptr, (const UInt8*)bytes.constData(), bytes.size());
    CFMutableDictionaryRef q = baseQuery(service, account);
    CFMutableDictionaryRef upd = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(upd, kSecValueData, data);
    OSStatus st = SecItemUpdate(q, upd);                                        // si ya existe se actualiza…
    if (st == errSecItemNotFound) { CFDictionaryAddValue(q, kSecValueData, data); st = SecItemAdd(q, nullptr); }   // …y si no, se crea
    CFRelease(upd); CFRelease(q); CFRelease(data);
    return st == errSecSuccess;
}

bool Keychain::get (const QString& service, const QString& account, QString* secret)
{
    CFMutableDictionaryRef q = baseQuery(service, account);
    CFDictionaryAddValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionaryAddValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef out = nullptr;
    OSStatus st = SecItemCopyMatching(q, &out);
    CFRelease(q);
    if (st != errSecSuccess || !out) return false;
    CFDataRef d = (CFDataRef)out;
    if (secret) *secret = QString::fromUtf8((const char*)CFDataGetBytePtr(d), (qsizetype)CFDataGetLength(d));
    CFRelease(out);
    return true;
}

bool Keychain::remove (const QString& service, const QString& account)
{
    CFMutableDictionaryRef q = baseQuery(service, account);
    OSStatus st = SecItemDelete(q);
    CFRelease(q);
    return st == errSecSuccess || st == errSecItemNotFound;
}
