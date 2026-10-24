// Weather.xapp — current conditions + 3-day forecast via com.freebsdx.Weather.

#include <xkit/application.h>
#include <xkit/widget.h>
#include <xkit/xbus_client.h>
#include <shared/protocol.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace fbx::xkit;
using namespace fbx::xrender;
using namespace fbx::shared;

// ── Background color from temperature ────────────────────────────────────────

static Color tempColor(int temp_c) noexcept {
    if (temp_c >= 30) return {0.55f, 0.25f, 0.10f, 1.0f};
    if (temp_c >= 20) return {0.25f, 0.35f, 0.55f, 1.0f};
    if (temp_c >= 10) return {0.15f, 0.28f, 0.42f, 1.0f};
    return {0.12f, 0.18f, 0.35f, 1.0f};
}

// ── Short weekday from YYYY-MM-DD ─────────────────────────────────────────────

static std::string shortDay(const std::string& date) noexcept {
    if (date.size() < 10) return date;
    struct tm tm{};
    int y, m, d;
    if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return date;
    tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
    mktime(&tm);
    const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    return days[tm.tm_wday % 7];
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int /*argc*/, char* /*argv*/[]) {
    // ── Location bar ──────────────────────────────────────────────────────────
    auto loc_input = std::make_shared<TextInputWidget>("Enter city…");
    loc_input->setFlexGrow(1.0f);

    auto on_search = std::make_shared<std::function<void()>>();
    auto search_btn = std::make_shared<ButtonWidget>("Search",
        [on_search]() { if (*on_search) (*on_search)(); });
    search_btn->setPadding(8.0f);
    search_btn->setBackgroundColor(Color{0.22f, 0.35f, 0.60f, 1.0f});
    search_btn->setColor(Color{1.0f, 1.0f, 1.0f, 1.0f});

    auto loc_row = std::make_shared<FlexRow>();
    loc_row->setGap(8.0f);
    loc_row->setPadding(12.0f);
    loc_row->setCrossAxisAlignment(CrossAxisAlignment::Center);
    loc_row->addChild(loc_input);
    loc_row->addChild(search_btn);

    // ── Main card (temperature + condition) ───────────────────────────────────
    auto temp_lbl = std::make_shared<TextWidget>("--°C", 64.0f);
    temp_lbl->setFontWeight(300);
    temp_lbl->setColor(Color{1.0f, 1.0f, 1.0f, 1.0f});

    auto icon_lbl = std::make_shared<TextWidget>("🌡", 48.0f);

    auto temp_icon_row = std::make_shared<FlexRow>();
    temp_icon_row->setGap(16.0f);
    temp_icon_row->setCrossAxisAlignment(CrossAxisAlignment::Center);
    temp_icon_row->setMainAxisAlignment(MainAxisAlignment::Center);
    temp_icon_row->addChild(temp_lbl);
    temp_icon_row->addChild(icon_lbl);

    auto cond_lbl = std::make_shared<TextWidget>("Fetching…", 16.0f);
    cond_lbl->setColor(Color{0.85f, 0.88f, 1.00f, 1.0f});

    auto city_lbl = std::make_shared<TextWidget>("", 12.0f);
    city_lbl->setColor(Color{0.65f, 0.70f, 0.85f, 1.0f});

    auto updated_lbl = std::make_shared<TextWidget>("", 10.0f);
    updated_lbl->setColor(Color{0.50f, 0.55f, 0.70f, 1.0f});

    auto card_col = std::make_shared<FlexColumn>();
    card_col->setPadding(20.0f);
    card_col->setGap(6.0f);
    card_col->setCrossAxisAlignment(CrossAxisAlignment::Center);
    card_col->addChild(temp_icon_row);
    card_col->addChild(cond_lbl);
    card_col->addChild(city_lbl);
    card_col->addChild(updated_lbl);

    auto card = std::make_shared<ContainerWidget>(card_col);
    card->setBackgroundColor(tempColor(0));
    card->setMarginAll(12.0f);

    // ── Details grid ──────────────────────────────────────────────────────────
    auto mk_detail = [](const char* label) {
        auto col = std::make_shared<FlexColumn>();
        col->setGap(2.0f);
        col->setCrossAxisAlignment(CrossAxisAlignment::Center);
        col->setFlexGrow(1.0f);

        auto lbl = std::make_shared<TextWidget>(std::string(label), 10.0f);
        lbl->setColor(Color{0.50f, 0.52f, 0.65f, 1.0f});

        auto val = std::make_shared<TextWidget>("--", 14.0f);
        val->setFontWeight(600);
        val->setColor(Color{0.88f, 0.90f, 1.00f, 1.0f});

        col->addChild(val);
        col->addChild(lbl);
        return std::make_pair(col, val);
    };

    auto [hum_col,  hum_val]  = mk_detail("Humidity");
    auto [wind_col, wind_val] = mk_detail("Wind");
    auto [feel_col, feel_val] = mk_detail("Feels Like");
    auto [uv_col,   uv_val]   = mk_detail("UV Index");

    auto details_row = std::make_shared<FlexRow>();
    details_row->setPadding(12.0f);
    details_row->setBackgroundColor(Color{0.14f, 0.15f, 0.22f, 1.0f});
    details_row->setMarginAll(12.0f);
    details_row->addChild(hum_col);
    details_row->addChild(wind_col);
    details_row->addChild(feel_col);
    details_row->addChild(uv_col);

    // ── 3-day forecast row ────────────────────────────────────────────────────
    auto forecast_row = std::make_shared<FlexRow>();
    forecast_row->setPadding(12.0f);
    forecast_row->setGap(4.0f);
    forecast_row->setMarginAll(12.0f);

    struct FcDay {
        std::shared_ptr<TextWidget> day_lbl, icon_lbl, max_lbl, min_lbl;
        std::shared_ptr<FlexColumn> col;
    };
    std::vector<FcDay> fc_days(3);
    for (int i = 0; i < 3; ++i) {
        auto& fd = fc_days[static_cast<size_t>(i)];
        fd.day_lbl  = std::make_shared<TextWidget>("---", 11.0f);
        fd.day_lbl->setColor(Color{0.60f, 0.62f, 0.75f, 1.0f});

        fd.icon_lbl = std::make_shared<TextWidget>("🌡", 22.0f);

        fd.max_lbl  = std::make_shared<TextWidget>("--°", 13.0f);
        fd.max_lbl->setFontWeight(600);
        fd.max_lbl->setColor(Color{0.92f, 0.93f, 1.00f, 1.0f});

        fd.min_lbl  = std::make_shared<TextWidget>("--°", 11.0f);
        fd.min_lbl->setColor(Color{0.55f, 0.58f, 0.72f, 1.0f});

        fd.col = std::make_shared<FlexColumn>();
        fd.col->setGap(3.0f);
        fd.col->setCrossAxisAlignment(CrossAxisAlignment::Center);
        fd.col->setFlexGrow(1.0f);
        fd.col->setPadding(8.0f);
        fd.col->setBackgroundColor(Color{0.14f, 0.15f, 0.22f, 1.0f});
        fd.col->addChild(fd.day_lbl);
        fd.col->addChild(fd.icon_lbl);
        fd.col->addChild(fd.max_lbl);
        fd.col->addChild(fd.min_lbl);

        forecast_row->addChild(fd.col);
        if (i < 2) forecast_row->addChild(std::make_shared<Spacer>(8, 1));
    }

    // ── Root ──────────────────────────────────────────────────────────────────
    auto root_col = std::make_shared<FlexColumn>();
    root_col->addChild(loc_row);
    root_col->addChild(card);
    root_col->addChild(details_row);
    root_col->addChild(forecast_row);

    auto root = std::make_shared<ContainerWidget>(root_col);
    root->setBackgroundColor(Color{0.10f, 0.11f, 0.17f, 1.0f});

    return XApplication::Run(root, "/tmp/freebsdx.sock", false, false,
        [&](XBusClient& xbus) {
            XApplication::setTitle("Weather");

            // ── Apply incoming WeatherCondition ───────────────────────────────
            auto apply_current = [card, temp_lbl, icon_lbl, cond_lbl,
                                   city_lbl, updated_lbl,
                                   hum_val, wind_val, feel_val, uv_val](
                    const WeatherCondition& w) {
                char tbuf[16];
                std::snprintf(tbuf, sizeof(tbuf), "%d°C", w.temp_c_);
                temp_lbl->setText(tbuf);
                icon_lbl->setText(w.icon_[0] ? w.icon_ : "🌡");
                cond_lbl->setText(w.condition_[0] ? w.condition_ : "Unknown");
                city_lbl->setText(w.city_[0] ? w.city_ : "");

                time_t now = std::time(nullptr);
                char ts[12];
                std::strftime(ts, sizeof(ts), "%H:%M", std::localtime(&now));
                updated_lbl->setText(std::string("Updated ") + ts);

                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u%%", w.humidity_);
                hum_val->setText(buf);
                std::snprintf(buf, sizeof(buf), "%u km/h %s", w.wind_kph_, w.wind_dir_);
                wind_val->setText(buf);
                std::snprintf(buf, sizeof(buf), "%d°C", w.feels_c_);
                feel_val->setText(buf);
                std::snprintf(buf, sizeof(buf), "%u", w.uv_index_);
                uv_val->setText(buf);

                card->setBackgroundColor(tempColor(w.temp_c_));
            };

            // ── Apply incoming forecast page ──────────────────────────────────
            auto apply_forecast = [&fc_days](const WeatherForecastPage& fp) {
                if (fp.count_ == 0 || fp.page_ >= 3) return;
                auto& fd = fc_days[fp.page_];
                const auto& d = fp.day_;
                fd.day_lbl->setText(d.date_[0] ? shortDay(d.date_) : "---");
                fd.icon_lbl->setText(d.icon_[0] ? d.icon_ : "🌡");
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d°", d.max_c_);
                fd.max_lbl->setText(buf);
                std::snprintf(buf, sizeof(buf), "%d°", d.min_c_);
                fd.min_lbl->setText(buf);
            };

            // ── Fetch on startup ──────────────────────────────────────────────
            xbus.Call("com.freebsdx.Weather",
                static_cast<uint32_t>(WeatherOpcode::GetCurrent), {},
                [apply_current](const XBusMessage& r) {
                    WeatherCondition w{};
                    std::memcpy(&w, r.payload_, sizeof(w));
                    apply_current(w);
                });

            xbus.Call("com.freebsdx.Weather",
                static_cast<uint32_t>(WeatherOpcode::GetForecast), {},
                [apply_forecast](const XBusMessage& r) {
                    WeatherForecastPage fp{};
                    std::memcpy(&fp, r.payload_, sizeof(fp));
                    apply_forecast(fp);
                });

            // ── Search button ─────────────────────────────────────────────────
            *on_search = [&xbus, loc_input]() {
                std::string city = loc_input->text();
                if (city.empty()) return;
                WeatherSetLocationRequest req{};
                std::strncpy(req.city_, city.c_str(), sizeof(req.city_) - 1);
                xbus.Call("com.freebsdx.Weather",
                    static_cast<uint32_t>(WeatherOpcode::SetLocation),
                    {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
                    nullptr);
            };

            // ── WeatherChanged broadcast ──────────────────────────────────────
            xbus.OnBroadcast([apply_current, apply_forecast](const XBusMessage& msg) {
                if (std::strcmp(msg.service_name_, "com.freebsdx.Weather") != 0) return;

                if (msg.opcode_ == static_cast<uint32_t>(WeatherOpcode::WeatherChanged)) {
                    WeatherCondition w{};
                    std::memcpy(&w, msg.payload_, sizeof(w));
                    apply_current(w);
                } else if (msg.opcode_ == static_cast<uint32_t>(WeatherOpcode::GetForecast)) {
                    WeatherForecastPage fp{};
                    std::memcpy(&fp, msg.payload_, sizeof(fp));
                    apply_forecast(fp);
                }
            });
        });
}
