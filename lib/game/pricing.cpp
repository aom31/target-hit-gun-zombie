#include "pricing.h"

#include <cstring>

namespace tr::game {

const char* PriceList::validate() const {
  if (durationsS.empty() || durationsS.size() > kMaxDurations) return "เวลาเล่นต้องมี 1–6 แบบ";
  for (size_t i = 0; i < durationsS.size(); ++i) {
    if (durationsS[i] < kMinDurationS || durationsS[i] > kMaxDurationS) return "เวลาเล่นต้องอยู่ระหว่าง 10–600 วินาที";
    if (i > 0 && durationsS[i] <= durationsS[i - 1]) return "เวลาเล่นต้องเรียงจากน้อยไปมาก และห้ามซ้ำ";
  }
  if (trialDurationS < kMinDurationS || trialDurationS > kMaxDurationS) return "เวลาทดลองเล่นต้องอยู่ระหว่าง 10–600 วินาที";
  if (trialPrice > kMaxPrice) return "ราคาทดลองเล่นสูงเกินไป";
  if (guns.size() > kMaxGuns) return "มีปืนได้ไม่เกิน 12 รายการ";
  for (const Gun& g : guns) {
    if (g.name.empty() || g.name.size() > kMaxGunNameBytes) return "ชื่อปืนต้องไม่ว่าง และยาวไม่เกิน 16 ตัวอักษร";
    if (g.note.size() > kMaxGunNoteBytes) return "หมายเหตุปืนยาวเกินไป";
    if (g.prices.size() != durationsS.size()) return "ราคาปืนต้องมีครบทุกเวลาเล่น";
    for (uint32_t p : g.prices) {
      if (p > kMaxPrice) return "ราคาสูงเกินไป";
    }
  }
  if (enabled && guns.empty()) return "เปิดระบบรับเงินต้องมีปืนอย่างน้อย 1 รายการ";
  return nullptr;
}

int PriceList::durationIndex(uint16_t seconds) const {
  for (size_t i = 0; i < durationsS.size(); ++i) {
    if (durationsS[i] == seconds) return static_cast<int>(i);
  }
  return -1;
}

PriceList defaultPriceList() {
  PriceList list;
  const std::vector<uint32_t> zero(list.durationsS.size(), 0);
  list.guns = {
      {"AR GBB", GunKind::Long, "ต้องเปลี่ยนแม็ก", zero},
      {"AR AEG", GunKind::Electric, "ปืนไฟฟ้า ไม่ต้องเปลี่ยนแม็ก", zero},
      {"ปืนสั้น GBB", GunKind::Short, "ต้องเปลี่ยนแม็ก", zero},
  };
  return list;
}

Quote quote(const PriceList& list, const QuoteRequest& req) {
  Quote q;
  const uint8_t players = req.players == 0 ? 1 : (req.players > kMaxPlayers ? kMaxPlayers : req.players);
  int column = -1;
  if (req.trial) {
    q.durationS = list.trialDurationS;
  } else {
    column = list.durationIndex(req.durationS);
    if (column < 0) {
      q.error = QuoteError::BadDuration;
      return q;
    }
    q.durationS = req.durationS;
  }

  for (uint8_t p = 0; p < players; ++p) {
    const int g = req.gun[p];
    if (!list.guns.empty()) {
      if (g < 0) {
        q.error = QuoteError::NoGun;
        return q;
      }
      if (g >= static_cast<int>(list.guns.size())) {
        q.error = QuoteError::BadGun;
        return q;
      }
    }
    uint32_t price = 0;
    if (req.trial) {
      price = list.trialPrice;
    } else if (g >= 0 && static_cast<size_t>(column) < list.guns[g].prices.size()) {
      price = list.guns[g].prices[column];
    }
    q.price[p] = price;
    q.total += price;
  }
  return q;
}

const char* toString(GunKind k) {
  switch (k) {
    case GunKind::Long: return "long";
    case GunKind::Short: return "short";
    case GunKind::Electric: return "electric";
  }
  return "long";
}

bool parseGunKind(const char* s, GunKind& out) {
  if (s == nullptr) return false;
  if (std::strcmp(s, "long") == 0) out = GunKind::Long;
  else if (std::strcmp(s, "short") == 0) out = GunKind::Short;
  else if (std::strcmp(s, "electric") == 0) out = GunKind::Electric;
  else return false;
  return true;
}

}  // namespace tr::game
