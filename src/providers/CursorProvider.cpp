#include "providers/CursorProvider.h"
#include "providers/HttpJson.h"
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <time.h>

// UNDOCUMENTED CURSOR WEB API. Mirrors E:\Cursor_Usage's read-only flow.
// Cursor does not guarantee this endpoint or schema; it may change without notice.
static String normalizeSessionToken(String token) {
  token.trim();
  const String prefix = "WorkosCursorSessionToken=";
  if (token.startsWith(prefix)) token.remove(0, prefix.length());
  token.replace("%3A%3A", "::"); token.replace("%3a%3a", "::");
  if (token.indexOf("::") >= 0) return token;
  int first=token.indexOf('.'),second=first<0?-1:token.indexOf('.',first+1);
  if(first>0&&second>first){
    String encoded=token.substring(first+1,second);encoded.replace('-','+');encoded.replace('_','/');while(encoded.length()%4)encoded+='=';
    size_t outputLength=0;unsigned char decoded[768];
    if(mbedtls_base64_decode(decoded,sizeof(decoded)-1,&outputLength,(const unsigned char*)encoded.c_str(),encoded.length())==0){
      decoded[outputLength]=0;JsonDocument payload;if(deserializeJson(payload,decoded)==DeserializationError::Ok){String subject=String((const char*)(payload["sub"]|""));if(subject.length())return subject+"::"+token;}
    }
  }
  return token;
}
static String cookieFor(String token) {
  const String prefix = "WorkosCursorSessionToken=";
  token=normalizeSessionToken(token);
  token.replace("::", "%3A%3A");
  return prefix + token;
}
static time_t parseIsoUtc(const char *iso) {
  if (!iso || !*iso) return 0; struct tm t = {};
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) return 0;
  t.tm_year -= 1900; t.tm_mon -= 1;
  // Arduino-ESP32 2.x does not expose timegm(). The device clock is configured
  // as UTC, so mktime provides the required epoch value portably here.
  setenv("TZ", "UTC0", 1); tzset(); return mktime(&t);
}
static String remainingText(const char *iso) {
  time_t end=parseIsoUtc(iso), now=time(nullptr); if(end<=now||now<1700000000)return "reset time unavailable";
  uint32_t s=end-now,d=s/86400; s%=86400; uint32_t h=s/3600,m=(s%3600)/60;
  return d ? "reset in "+String(d)+"d "+String(h)+"h" : "reset in "+String(h)+"h "+String(m)+"m";
}
UsageSnapshot CursorProvider::fetch(const ProviderConfig &cfg, bool tls) {
  UsageSnapshot out; out.provider="Cursor"; out.primary.label="CURSOR MODELS"; out.secondary.label="AUTO MODELS"; out.tertiary.label="API USAGE";
  if(!cfg.enabled){out.status="disabled";return out;} if(!cfg.token.length()){out.status="auth token missing";return out;}
  ProviderConfig request=cfg; request.token=""; request.session=cookieFor(cfg.token);
  JsonDocument d; String error; String url=cfg.endpoint.length()?cfg.endpoint:"https://cursor.com/api/usage-summary";
  if(!getJson(url,request,tls,d,error)){out.status=error;return out;}
  float autoUsed=d["individualUsage"]["plan"]["autoPercentUsed"]|-1.0f;
  float apiUsed=d["individualUsage"]["plan"]["apiPercentUsed"]|-1.0f;
  float totalUsed=d["individualUsage"]["plan"]["totalPercentUsed"]|-1.0f;
  if(totalUsed<0) totalUsed=max(autoUsed,apiUsed);
  String reset=remainingText(d["billingCycleEnd"]|"");
  out.primary.usedPercent=totalUsed; out.secondary.usedPercent=autoUsed; out.tertiary.usedPercent=apiUsed;
  out.primary.resetText=reset; out.secondary.resetText=reset; out.tertiary.resetText=reset;
  out.plan=String((const char*)(d["membershipType"]|"")); out.ok=out.primary.usedPercent>=0;
  out.status=out.ok?"online (unofficial)":"usage fields missing"; return out;
}

