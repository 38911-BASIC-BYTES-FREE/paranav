#include "flight.h"
#include "i18n.h"

#include <algorithm>

#include "terrain.h"

namespace nav {

static float stdToQnhAlt(float altStd, float qnh) {
    float p = 1013.25f * std::pow(1.0f - altStd / 44330.77f, 1.0f / 0.190263f);
    return baroAltitude(p, qnh);
}

void FlightComputer::common(const BaroSnapshot& b, uint32_t uptimeS) {
    st_.uptimeS = uptimeS;
    st_.baroOk = b.ok;
    st_.altSource = b.ok ? (b.source == AltSource::None ? AltSource::Wired : b.source)
                         : (st_.fix ? AltSource::Gps : AltSource::None);
    if (b.ok) {
        st_.pressHpa = b.pressHpa;
        st_.tempC = b.tempC;
        st_.altStd = b.altStd;
        st_.qnh = set_.qnhHpa;
        st_.altMsl = stdToQnhAlt(b.altStd, set_.qnhHpa);
        st_.vario = b.vario;
        st_.vario1s = b.vario;
    } else {
        // no barometer: altitude from GPS, vario from a slow Kalman filter on GPS altitude
        st_.altMsl = st_.fix ? st_.gpsAlt : NAN;
        st_.altStd = st_.altMsl;
        st_.vario = st_.vario1s = (st_.fix && gpsKv_.initialized()) ? gpsKv_.vario() : 0.f;
    }
    if (st_.homeSet && !std::isnan(st_.homeElev) && !std::isnan(st_.altMsl))
        st_.aglTakeoff = st_.altMsl - st_.homeElev;
    else
        st_.aglTakeoff = NAN;
    st_.aglTerrain = (!std::isnan(st_.groundMsl) && !std::isnan(st_.altMsl)) ? st_.altMsl - st_.groundMsl : NAN;
    if (st_.phase == Phase::Flying) st_.flightTimeS = uptimeS - takeoffUptime_;
}

void FlightComputer::update(const GpsFix& g, const BaroSnapshot& b, uint32_t uptimeS) {
    takeoffEvent = landingEvent = false;
    if (pendingLand_) {  // "LANDED" answered since the last update
        pendingLand_ = false;
        if (st_.phase == Phase::Flying) landNow();
    }
    float dt = lastUptime_ ? (float)(uptimeS - lastUptime_) : 1.f;
    if (dt < 0.5f) dt = 0.5f;
    if (dt > 5.f) dt = 5.f;
    lastUptime_ = uptimeS;

    st_.fix = g.valid;
    st_.sats = g.sats;
    st_.hdop = g.hdop;
    if (g.year >= 2020) {
        st_.timeValid = true;
        st_.year = g.year; st_.month = g.month; st_.day = g.day;
        st_.hour = g.hour; st_.minute = g.minute; st_.second = g.second;
        st_.utcSec = g.secOfDay();
        st_.dateYmd = g.year * 10000u + g.month * 100u + g.day;
    }
    if (g.valid) {
        st_.fixAgeS = 0;
        st_.lat = g.lat;
        st_.lon = g.lon;
        st_.gpsAlt = g.altMsl;
        st_.gsKmh = g.speedKmh;
        if (g.courseValid && g.speedKmh > 3) st_.trackDeg = g.courseDeg;
        if (terrain_) st_.groundMsl = terrain_->elevation(g.lat, g.lon);
        gpsKv_.update(g.altMsl, dt);
    } else if (st_.fixAgeS < 999) {
        st_.fixAgeS++;
    }
    autoQnh(g);
    common(b, uptimeS);
    varioAvg30_.push(st_.vario);
    st_.varioAvg = varioAvg30_.avg();
    if (g.valid) {
        flightLogic(g);
        wind_.add(uptimeS, st_.trackDeg, st_.gsKmh);
    }
    st_.wind = wind_.result();
    common(b, uptimeS);  // refresh AGL after home may have changed
    navigation();
    fuel(dt);
}

void FlightComputer::updateNoGps(const BaroSnapshot& b, uint32_t uptimeS) {
    takeoffEvent = landingEvent = false;
    if (pendingLand_) {
        pendingLand_ = false;
        if (st_.phase == Phase::Flying) landNow();
    }
    float dt = lastUptime_ ? (float)(uptimeS - lastUptime_) : 1.f;
    if (dt <= 0) return;
    lastUptime_ = uptimeS;
    if (st_.fixAgeS < 999) st_.fixAgeS += (uint32_t)dt;
    if (st_.fixAgeS > 3) st_.fix = false;
    common(b, uptimeS);
    fuel(dt > 5 ? 5 : dt);
}

void FlightComputer::autoQnh(const GpsFix& g) {
    if (!set_.autoQnh || st_.qnhCalibrated || st_.phase != Phase::Ground) return;
    if (!st_.baroOk || !g.valid || g.sats < 6 || g.hdop > 2.0f) {
        qnhGood_ = 0;
        qnhAcc_ = 0;
        return;
    }
    float ref = g.altMsl;
    if (terrain_) {
        float t = terrain_->elevation(g.lat, g.lon);
        if (!std::isnan(t)) ref = t + 1.0f;  // device ~1 m above ground
    }
    qnhAcc_ += ref;
    if (++qnhGood_ >= 10) {
        float h = qnhAcc_ / qnhGood_;
        float p = 1013.25f * std::pow(1.0f - st_.altStd / 44330.77f, 1.0f / 0.190263f);
        set_.qnhHpa = qnhFromAltitude(p, h);
        st_.qnhCalibrated = true;
    }
}

void FlightComputer::setQnh(float qnh) {
    set_.qnhHpa = qnh;
    st_.qnhCalibrated = true;
}

void FlightComputer::setAltitude(float altMsl) {
    if (!st_.baroOk) return;
    float p = 1013.25f * std::pow(1.0f - st_.altStd / 44330.77f, 1.0f / 0.190263f);
    setQnh(qnhFromAltitude(p, altMsl));
}

void FlightComputer::flightLogic(const GpsFix& g) {
    // --- home follows the pilot while standing on the ground (until set manually)
    if (st_.phase == Phase::Ground && g.speedKmh < 5 && !manualHome_ && g.sats >= 5) {
        st_.homeSet = true;
        st_.homeLat = g.lat;
        st_.homeLon = g.lon;
        st_.homeElev = st_.altMsl;
    }
    // --- takeoff detection
    if (st_.phase == Phase::Ground) {
        if (g.speedKmh > 15) {
            if (fastCount_++ == 0) {
                candUptime_ = st_.uptimeS;
                candUtc_ = st_.utcSec;
            }
            if (fastCount_ >= 10) {
                st_.phase = Phase::Flying;
                takeoffEvent = true;
                st_.takeoffUtc = candUtc_;
                takeoffUptime_ = candUptime_;
                st_.flightTimeS = 0;
                st_.maxAlt = st_.altMsl;
                st_.maxClimb = st_.maxSink = 0;
                st_.distFlownM = st_.maxDistHomeM = 0;
                st_.fuelUsedL = 0;
                track_.clear();
                slowCount_ = 0;
                havePrev_ = false;
                wind_.reset();
            }
        } else {
            fastCount_ = 0;
        }
    }
    if (st_.phase != Phase::Flying) {
        recent_.clear();
        return;
    }
    // --- in flight statistics
    if (!std::isnan(st_.altMsl) && (std::isnan(st_.maxAlt) || st_.altMsl > st_.maxAlt)) st_.maxAlt = st_.altMsl;
    st_.maxClimb = std::max(st_.maxClimb, st_.vario1s);
    st_.maxSink = std::min(st_.maxSink, st_.vario1s);
    if (havePrev_) {
        double d = distanceM({prevLat_, prevLon_}, {g.lat, g.lon});
        if (d < 200) st_.distFlownM += (float)d;  // ignore jumps
    }
    prevLat_ = g.lat;
    prevLon_ = g.lon;
    havePrev_ = true;
    if (st_.homeSet) st_.maxDistHomeM = std::max(st_.maxDistHomeM, st_.homeDistM);

    // --- circling / thermal
    if (!std::isnan(lastTrack_) && st_.gsKmh > 5) turnRate_.push((float)wrap180(st_.trackDeg - lastTrack_));
    lastTrack_ = st_.trackDeg;
    bool circ = turnRate_.full() && std::fabs(turnRate_.avg()) > 6.f;
    if (circ && !st_.circling) {
        circleStartAlt_ = st_.altMsl;
        circleStartT_ = st_.uptimeS;
    }
    st_.circling = circ;
    if (circ && !std::isnan(circleStartAlt_)) {
        st_.thermalGainM = st_.altMsl - circleStartAlt_;
        uint32_t dt = st_.uptimeS - circleStartT_;
        st_.thermalAvg = dt > 5 ? st_.thermalGainM / dt : 0;
    }

    // --- track history
    TrackPt tp{(float)g.lat, (float)g.lon, st_.vario1s};
    recent_.push_back(tp);
    if (recent_.size() > 90) recent_.erase(recent_.begin());
    if ((trackTick_++ & 1) == 0) {
        track_.push_back(tp);
        if (track_.size() > 4000) {  // decimate: keep every second point
            std::vector<TrackPt> t;
            t.reserve(2100);
            for (size_t i = 0; i < track_.size(); i += 2) t.push_back(track_[i]);
            track_.swap(t);
        }
    }

    // --- landing detection (see the header: question instead of an immediate end)
    bool asking = st_.landingAsk;  // hysteresis: while asking, only clear movement closes the question
    bool still = g.speedKmh < (asking ? 8.f : 5.f) && std::fabs(st_.vario1s) < (asking ? 1.0f : 0.5f);
    if (still) {
        if (slowCount_++ == 0) stillSince_ = st_.uptimeS;
        st_.landingStillS = (uint16_t)std::min<uint32_t>(st_.uptimeS - stillSince_, 65535);
        if ((uint32_t)slowCount_ >= kLandAskS) {
            bool high = !std::isnan(st_.aglTerrain) && st_.aglTerrain >= kLandHighAglM;
            if (high) {
                st_.landingAsk = false;  // hovering well above the ground: certainly still flying
            } else if (!st_.landingAsk && st_.uptimeS >= askQuietUntil_) {
                st_.landingAsk = true;
                askSince_ = st_.uptimeS;
            }
            if (st_.landingAsk) {
                uint32_t el = st_.uptimeS - askSince_;
                st_.landingEndInS = (uint16_t)(el >= kLandAutoEndS ? 0 : kLandAutoEndS - el);
                if (el >= kLandAutoEndS) landNow();
            }
        }
    } else {
        slowCount_ = 0;
        st_.landingAsk = false;
        st_.landingStillS = st_.landingEndInS = 0;
    }
}

float FlightComputer::groundSpeedAlong(float brg, float va) const {
    if (!st_.wind.valid) return va;
    float rel = (float)((st_.wind.fromDeg + 180.0 - brg) * kDeg2Rad);
    float along = st_.wind.speedKmh * std::cos(rel), cross = st_.wind.speedKmh * std::sin(rel);
    float a2 = va * va - cross * cross;
    return (a2 > 0 ? std::sqrt(a2) : 0) + along;
}

float FlightComputer::glideArrival(double lat, double lon, float tgtElev) const {
    if (std::isnan(st_.altMsl) || std::isnan(tgtElev) || !st_.fix) return NAN;
    float d = (float)distanceM({st_.lat, st_.lon}, {lat, lon});
    float brg = (float)bearingDeg({st_.lat, st_.lon}, {lat, lon});
    float vg = groundSpeedAlong(brg, set_.glideSpeedKmh);
    if (vg < 1.f) return NAN;
    float sink = (set_.glideSpeedKmh / 3.6f) / std::max(1.f, set_.glideRatio);
    float loss = d / (vg / 3.6f) * sink;
    return st_.altMsl - loss - tgtElev;
}

float FlightComputer::glideRangeM(float dirDeg) const {
    float ground = !std::isnan(st_.groundMsl) ? st_.groundMsl : st_.homeElev;
    if (std::isnan(ground) || std::isnan(st_.altMsl)) return 0;
    float h = st_.altMsl - ground - set_.glideMarginM;
    if (h <= 0) return 0;
    float vg = groundSpeedAlong(dirDeg, set_.glideSpeedKmh);
    if (vg <= 0) return 0;
    float sink = (set_.glideSpeedKmh / 3.6f) / std::max(1.f, set_.glideRatio);
    return h / sink * (vg / 3.6f);
}

void FlightComputer::navigation() {
    // glide ratio (measured only while descending steadily)
    ldVario_.push(st_.vario1s);
    ldGs_.push(st_.gsKmh);
    if (ldVario_.full() && ldVario_.avg() < -0.4f && ldGs_.avg() > 5)
        st_.ldCurrent = (ldGs_.avg() / 3.6f) / -ldVario_.avg();
    else
        st_.ldCurrent = NAN;
    st_.ldUsed = (!st_.engineOn && !std::isnan(st_.ldCurrent)) ? st_.ldCurrent : set_.glideRatio;

    if (st_.homeSet && st_.fix) {
        st_.homeDistM = (float)distanceM({st_.lat, st_.lon}, {st_.homeLat, st_.homeLon});
        st_.homeBrg = (float)bearingDeg({st_.lat, st_.lon}, {st_.homeLat, st_.homeLon});
        float vg = groundSpeedAlong(st_.homeBrg, set_.cruiseKmh);
        st_.homeEteS = vg > 3 ? st_.homeDistM / (vg / 3.6f) : -1;
        st_.homeArrivalAgl = glideArrival(st_.homeLat, st_.homeLon, st_.homeElev);
        st_.homeReachable = !std::isnan(st_.homeArrivalAgl) && st_.homeArrivalAgl >= set_.glideMarginM;
    }
    updateTarget();
}

void FlightComputer::updateTarget() {
    st_.tgtValid = false;
    switch (st_.tgtKind) {
        case TargetKind::Home:
            if (st_.homeSet) {
                st_.tgtValid = true;
                st_.tgtName = TR("START", "HOME");
                st_.tgtLat = st_.homeLat;
                st_.tgtLon = st_.homeLon;
                st_.tgtElev = st_.homeElev;
            }
            break;
        case TargetKind::Waypoint:
            if (wpts_ && wptIdx_ >= 0 && wptIdx_ < (int)wpts_->all().size()) {
                const Waypoint& w = wpts_->all()[wptIdx_];
                st_.tgtValid = true;
                st_.tgtName = w.name;
                st_.tgtLat = w.lat;
                st_.tgtLon = w.lon;
                st_.tgtElev = w.elevM;
            }
            break;
        case TargetKind::Point:
            st_.tgtValid = true;
            st_.tgtName = ptName_;
            st_.tgtLat = ptLat_;
            st_.tgtLon = ptLon_;
            st_.tgtElev = NAN;
            break;
        case TargetKind::Landing:
            if (wpts_ && st_.fix) {
                int i = wpts_->nearestLanding(st_.lat, st_.lon);
                if (i >= 0) {
                    const Waypoint& w = wpts_->all()[i];
                    st_.tgtValid = true;
                    st_.tgtName = w.name;
                    st_.tgtLat = w.lat;
                    st_.tgtLon = w.lon;
                    st_.tgtElev = w.elevM;
                }
            }
            break;
        case TargetKind::Route:
            if (route_ && !route_->pts.empty()) {
                if (route_->active >= (int)route_->pts.size()) route_->active = (int)route_->pts.size() - 1;
                const Waypoint& w = route_->pts[route_->active];
                st_.tgtValid = true;
                st_.tgtName = w.name;
                st_.tgtLat = w.lat;
                st_.tgtLon = w.lon;
                st_.tgtElev = w.elevM;
                st_.routeIdx = route_->active;
                st_.routeLen = (int)route_->pts.size();
            }
            break;
    }
    if (!st_.tgtValid || !st_.fix) return;
    if (std::isnan(st_.tgtElev) || st_.tgtElev == 0) {
        if (terrain_) {
            float e = terrain_->elevation(st_.tgtLat, st_.tgtLon);
            if (!std::isnan(e)) st_.tgtElev = e;
        }
    }
    st_.tgtDistM = (float)distanceM({st_.lat, st_.lon}, {st_.tgtLat, st_.tgtLon});
    st_.tgtBrg = (float)bearingDeg({st_.lat, st_.lon}, {st_.tgtLat, st_.tgtLon});
    float vg = groundSpeedAlong(st_.tgtBrg, set_.cruiseKmh);
    st_.tgtEteS = vg > 3 ? st_.tgtDistM / (vg / 3.6f) : -1;
    float h = st_.altMsl - st_.tgtElev;
    st_.tgtReqLD = h > 1 ? st_.tgtDistM / h : NAN;
    st_.tgtArrivalAgl = glideArrival(st_.tgtLat, st_.tgtLon, st_.tgtElev);
    st_.tgtFuelArriveL = st_.tgtEteS >= 0 ? st_.fuelLeftL - st_.tgtEteS / 3600.f * set_.fuelFlowLph : NAN;
    // route auto-advance
    if (st_.tgtKind == TargetKind::Route && route_ && st_.tgtDistM < 200 && route_->active + 1 < (int)route_->pts.size())
        route_->active++;
}

void FlightComputer::fuel(float dt) {
    if (st_.phase == Phase::Flying && st_.engineOn) st_.fuelUsedL += set_.fuelFlowLph * dt / 3600.f;
    st_.fuelConfigured = set_.fuelFlowLph > 0.1f && set_.fuelStartL > 0;
    st_.fuelLeftL = std::max(0.f, set_.fuelStartL - st_.fuelUsedL);
    float flow = std::max(0.1f, set_.fuelFlowLph);
    st_.enduranceMin = st_.fuelLeftL / flow * 60.f;
    float eteH = (st_.homeSet && st_.homeEteS > 0) ? st_.homeEteS / 3600.f : 0;
    if (st_.homeSet && st_.homeEteS < 0 && st_.homeDistM > 500) eteH = 99;  // cannot make headway home
    st_.returnNeedL = eteH * flow + set_.fuelReserveMin / 60.f * flow;
    st_.returnMarginMin = (st_.fuelLeftL - st_.returnNeedL) / flow * 60.f;
}

void FlightComputer::setHomeHere() {
    if (!st_.fix) return;
    manualHome_ = true;
    st_.homeSet = true;
    st_.homeLat = st_.lat;
    st_.homeLon = st_.lon;
    st_.homeElev = !std::isnan(st_.groundMsl) ? st_.groundMsl : st_.altMsl;
}
void FlightComputer::setTargetHome() { st_.tgtKind = TargetKind::Home; }
void FlightComputer::setTargetWaypoint(int idx) {
    wptIdx_ = idx;
    st_.tgtKind = TargetKind::Waypoint;
}
void FlightComputer::setTargetPoint(const std::string& name, double lat, double lon) {
    ptName_ = name;
    ptLat_ = lat;
    ptLon_ = lon;
    st_.tgtKind = TargetKind::Point;
    updateTarget();
}
void FlightComputer::updateTargetPoint(double lat, double lon) {
    ptLat_ = lat;
    ptLon_ = lon;
}
void FlightComputer::setTargetNearestLanding() { st_.tgtKind = TargetKind::Landing; }
void FlightComputer::setTargetRoute(int idx) {
    if (!route_ || route_->pts.empty()) return;
    route_->active = std::max(0, std::min(idx, (int)route_->pts.size() - 1));
    st_.tgtKind = TargetKind::Route;
}
void FlightComputer::routeNext() {
    if (route_ && route_->active + 1 < (int)route_->pts.size()) route_->active++;
}
void FlightComputer::routePrev() {
    if (route_ && route_->active > 0) route_->active--;
}
void FlightComputer::refuel(float l) {
    set_.fuelStartL = l;
    st_.fuelUsedL = 0;
}
void FlightComputer::landNow() {
    // the flight ended when the movement stopped (not when the question was answered)
    if (slowCount_ > 0 && stillSince_ >= takeoffUptime_) st_.flightTimeS = stillSince_ - takeoffUptime_;
    st_.phase = Phase::Ground;
    landingEvent = true;
    lastFlight.valid = true;
    lastFlight.durationS = st_.flightTimeS;
    lastFlight.maxAlt = st_.maxAlt;
    lastFlight.maxClimb = st_.maxClimb;
    lastFlight.maxSink = st_.maxSink;
    lastFlight.distKm = st_.distFlownM / 1000.f;
    lastFlight.maxDistHomeKm = st_.maxDistHomeM / 1000.f;
    lastFlight.fuelUsedL = st_.fuelUsedL;
    st_.circling = false;
    st_.landingAsk = false;
    st_.landingStillS = st_.landingEndInS = 0;
    fastCount_ = slowCount_ = 0;
}

void FlightComputer::answerStillFlying() {
    st_.landingAsk = false;
    st_.landingStillS = st_.landingEndInS = 0;
    slowCount_ = 0;  // start counting again
    askQuietUntil_ = st_.uptimeS + kLandQuietS;
}

void FlightComputer::answerLanded() {
    if (st_.phase != Phase::Flying) return;
    pendingLand_ = true;      // landNow() at the next update (its landingEvent is seen by the main loop)
    st_.landingAsk = false;   // the card disappears right away
}

void FlightComputer::resetFlight() {
    st_.phase = Phase::Ground;
    fastCount_ = slowCount_ = 0;
    st_.landingAsk = false;
    st_.landingStillS = st_.landingEndInS = 0;
    pendingLand_ = false;
    manualHome_ = false;
    track_.clear();
    recent_.clear();
}

}  // namespace nav
