#include "ui/app_ui.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <cmath>
#include <algorithm>

namespace rp {

namespace {

ImVec4 status_color(TableStatus s) {
    switch (s) {
        case TableStatus::Free: return ImVec4(0.22f, 0.62f, 0.35f, 1);
        case TableStatus::Occupied: return ImVec4(0.22f, 0.42f, 0.82f, 1);
        case TableStatus::Ordering: return ImVec4(0.40f, 0.52f, 0.92f, 1);
        case TableStatus::Waiting: return ImVec4(0.88f, 0.62f, 0.12f, 1);
        case TableStatus::Bill: return ImVec4(0.82f, 0.28f, 0.52f, 1);
        case TableStatus::Dirty: return ImVec4(0.42f, 0.42f, 0.42f, 1);
    }
    return ImVec4(0.3f, 0.3f, 0.3f, 1);
}

// Waiter: 0=service (tables+order+pay), 1=receipts
// Manager: 0=floor 1=kitchen 2=stock 3=products 4=receipts 5=reports
struct UiState {
    int tab = 0;
    int selected_table = 1;
    int selected_session = 0;
    char pin[16] = {};
    char customer[64] = {};
    int covers = 2;
    char status_msg[256] = "Select role PIN to start";
    std::vector<CartLine> cart;
    int menu_filter_cat = 0;
    char search[64] = {};
    int mod_item_id = 0;
    bool show_mods = false;
    std::vector<Modifier> mod_options;
    bool mod_sel[32] = {};
    char line_note[64] = {};
    bool rush = false;
    double pay_discount = 0;
    double pay_tip = 0;
    int pay_method = 0; // 0 cash 1 card
    int selected_receipt = 0;
    int stock_sel = 0;
    float receive_qty = 100;
    char stock_note[64] = "delivery";
    std::string receipt_text;
    std::string last_receipt_no;
    double last_receipt_total = 0;
    bool show_checkout = false;

    // Manager product form
    char prod_name[80] = {};
    float prod_price = 5.0f;
    int prod_cat_idx = 0;
    int prod_type = 0; // 0 food 1 drink
    char prod_size[24] = {};
    char prod_allergens[64] = {};
    int prod_ing_id = 0;
    float prod_ing_qty = 1.0f;
    int prod_list_sel = 0;
};

void set_msg(UiState& ui, const std::string& m) {
    std::snprintf(ui.status_msg, sizeof(ui.status_msg), "%s", m.c_str());
}

DiningTable find_table(const std::vector<DiningTable>& tables, int id) {
    for (const auto& t : tables) if (t.id == id) return t;
    return {};
}

void format_receipt(const Receipt& r, std::string& out) {
    std::ostringstream oss;
    oss << "======== " << r.receipt_no << " ========\n";
    oss << "RestoPulse Cafe\n" << r.issued_at << "\n";
    if (!r.customer_name.empty()) oss << "Customer: " << r.customer_name << "\n";
    oss << "--------------------------------\n";
    for (auto& l : r.lines) {
        oss << l.second.first << " x " << l.first << "  @" << l.second.second
            << "  = " << (l.second.first * l.second.second) << "\n";
    }
    oss << "--------------------------------\n";
    oss << "Subtotal: $" << r.subtotal << "\n";
    if (r.discount) oss << "Discount: -$" << r.discount << "\n";
    oss << "Tax: $" << r.tax << "\n";
    if (r.tip) oss << "Tip: $" << r.tip << "\n";
    oss << "TOTAL: $" << r.total << "\n";
    oss << (r.status == ReceiptStatus::Void ? "VOID\n" : "PAID\n");
    out = oss.str();
}

double session_due(const Session& sess) {
    double due = 0;
    for (const auto& o : sess.orders) {
        if (o.status == OrderStatus::Void) continue;
        for (const auto& l : o.lines) {
            if (l.status == OrderStatus::Void) continue;
            due += l.unit_price * l.qty;
        }
    }
    return due;
}

// ---------- shared helpers ----------
void draw_table_tiles(PosApp& app, UiState& ui, float height = 220.f) {
    auto tables = app.tables();
    ImGui::BeginChild("tables_row", ImVec2(0, height), true);
    ImGui::Text("Tables");
    ImGui::SameLine();
    ImGui::TextDisabled("  free=green  busy=blue  food ready wait=orange  bill=pink  dirty=gray");
    ImGui::Separator();

    const float btn_w = 118.f, btn_h = 78.f;
    for (size_t i = 0; i < tables.size(); ++i) {
        const auto& t = tables[i];
        if (i > 0) ImGui::SameLine();
        if (i == 5) ImGui::NewLine(); // second row

        ImGui::PushID(t.id);
        ImGui::PushStyleColor(ImGuiCol_Button, status_color(t.status));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(
            status_color(t.status).x + 0.08f,
            status_color(t.status).y + 0.08f,
            status_color(t.status).z + 0.08f, 1));
        char label[96];
        std::snprintf(label, sizeof(label), "%s\n%s\n$%.2f",
                      t.label.c_str(), to_string(t.status), t.running_total);
        if (ImGui::Button(label, ImVec2(btn_w, btn_h))) {
            ui.selected_table = t.id;
            ui.selected_session = t.open_session_id;
            std::snprintf(ui.customer, sizeof(ui.customer), "%s", t.customer_name.c_str());
            ui.covers = t.covers > 0 ? t.covers : 2;
            ui.show_checkout = (t.status == TableStatus::Bill);
            ui.cart.clear();
        }
        if (ui.selected_table == t.id) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(255, 255, 255, 255), 6.f, 0, 3.f);
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    ImGui::EndChild();
}

bool do_pay(PosApp& app, UiState& ui, int session_id, int table_id) {
    auto sess = app.session_for_table(table_id);
    if (!sess || sess->id != session_id) {
        set_msg(ui, "No open session");
        return false;
    }
    double due = session_due(*sess);
    due = std::max(0.0, due - ui.pay_discount);
    double tax = due * 0.10;
    double total = due + tax + ui.pay_tip;
    PayMethod m = ui.pay_method == 1 ? PayMethod::Card : PayMethod::Cash;
    auto res = app.pay(session_id, ui.pay_discount, ui.pay_tip, m, total + 0.001, ui.customer);
    if (!res.ok) {
        set_msg(ui, res.error);
        return false;
    }
    format_receipt(res.receipt, ui.receipt_text);
    ui.last_receipt_no = res.receipt.receipt_no;
    ui.last_receipt_total = res.receipt.total;
    ui.selected_receipt = res.receipt.id;
    ui.selected_session = 0;
    ui.show_checkout = false;
    ui.cart.clear();
    ui.pay_discount = 0;
    ui.pay_tip = 0;
    set_msg(ui, "Paid " + res.receipt.receipt_no + "  $" + std::to_string(res.receipt.total) +
                    " — free the table when cleared");
    return true;
}

void draw_order_panel(PosApp& app, UiState& ui, bool compact_pay) {
    if (!ui.selected_session) {
        ImGui::TextDisabled("Open a table session to take orders.");
        return;
    }

    auto menu = app.menu();
    auto cats = app.hub().ledger().categories();

    ImGui::BeginChild("menu_col", ImVec2(ImGui::GetContentRegionAvail().x * 0.52f, 0), true);
    ImGui::Text("Menu");
    if (ImGui::BeginCombo("Category", ui.menu_filter_cat == 0 ? "All" : [&]() {
            for (auto& c : cats) if (c.first == ui.menu_filter_cat) return c.second.c_str();
            return "All";
        }())) {
        if (ImGui::Selectable("All", ui.menu_filter_cat == 0)) ui.menu_filter_cat = 0;
        for (auto& c : cats)
            if (ImGui::Selectable(c.second.c_str(), ui.menu_filter_cat == c.first))
                ui.menu_filter_cat = c.first;
        ImGui::EndCombo();
    }
    ImGui::InputTextWithHint("##search", "Search…", ui.search, sizeof(ui.search));
    ImGui::Checkbox("Rush order", &ui.rush);
    if (app.happy_hour()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "HAPPY HOUR");
    }
    ImGui::Separator();

    for (const auto& m : menu) {
        if (ui.menu_filter_cat && m.category_id != ui.menu_filter_cat) continue;
        if (ui.search[0] && m.name.find(ui.search) == std::string::npos) continue;
        double price = app.hub().ledger().price_for(m, app.happy_hour());
        std::string why;
        bool avail = app.item_available(m.id, 1, &why);
        ImGui::PushStyleColor(ImGuiCol_Text, avail ? ImVec4(1, 1, 1, 1) : ImVec4(1, 0.45f, 0.45f, 1));
        char label[160];
        std::snprintf(label, sizeof(label), "%s   $%.2f%s", m.name.c_str(), price, avail ? "" : "  (86)");
        if (ImGui::Selectable(label) && avail) {
            ui.mod_item_id = m.id;
            ui.mod_options = app.modifiers(m.id);
            std::memset(ui.mod_sel, 0, sizeof(ui.mod_sel));
            ui.line_note[0] = 0;
            if (ui.mod_options.empty()) {
                CartLine cl;
                cl.menu_item_id = m.id;
                cl.name = m.name;
                cl.qty = 1;
                cl.unit_price = price;
                ui.cart.push_back(cl);
                set_msg(ui, "Added " + m.name);
            } else {
                ui.show_mods = true;
            }
        }
        ImGui::PopStyleColor();
        if (!avail && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", why.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("cart_col", ImVec2(0, 0), true);
    ImGui::Text("Current order");
    ImGui::Separator();
    double cart_total = 0;
    for (size_t i = 0; i < ui.cart.size(); ++i) {
        auto& c = ui.cart[i];
        cart_total += c.unit_price * c.qty;
        ImGui::Text("%dx %s   $%.2f", c.qty, c.name.c_str(), c.unit_price * c.qty);
        if (!c.modifier_names.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(");
            for (size_t j = 0; j < c.modifier_names.size(); ++j) {
                if (j) { ImGui::SameLine(); ImGui::TextDisabled(","); }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", c.modifier_names[j].c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled(")");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(("Remove##" + std::to_string(i)).c_str())) {
            ui.cart.erase(ui.cart.begin() + static_cast<long>(i));
            --i;
        }
    }
    ImGui::Separator();
    ImGui::Text("Cart: $%.2f", cart_total);

    if (ImGui::Button("Send order", ImVec2(-1, 44))) {
        if (ui.cart.empty()) set_msg(ui, "Cart is empty");
        else {
            auto res = app.place_order(ui.selected_session, ui.cart, ui.rush, "");
            if (res.ok) {
                set_msg(ui, "Order #" + std::to_string(res.order_id) + " sent — stock updated");
                ui.cart.clear();
                app.hub().ledger().set_table_status(ui.selected_table, TableStatus::Waiting);
                app.hub().sync_hot_from_ledger();
            } else {
                std::string e = res.error;
                for (auto& m : res.missing) e += " | " + m.name;
                set_msg(ui, e);
            }
        }
    }
    if (ImGui::Button("Clear cart", ImVec2(-1, 0))) ui.cart.clear();

    // Open session bill summary
    if (auto sess = app.session_for_table(ui.selected_table)) {
        double due = session_due(*sess);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Table total: $%.2f", due);
        for (const auto& o : sess->orders) {
            if (o.status == OrderStatus::Void) continue;
            ImGui::BulletText("Order #%d [%s]", o.id, to_string(o.status));
            for (const auto& l : o.lines) {
                if (l.status == OrderStatus::Void) continue;
                ImGui::Text("   %dx %s  $%.2f", l.qty, l.name.c_str(), l.unit_price * l.qty);
            }
        }
        if (compact_pay) {
            ImGui::Spacing();
            if (ImGui::Button("Checkout / Receipt", ImVec2(-1, 48))) {
                ui.show_checkout = true;
                ui.pay_method = 0;
            }
        }
    }
    ImGui::EndChild();

    if (ui.show_mods) {
        ImGui::OpenPopup("Options");
        ui.show_mods = false;
    }
    if (ImGui::BeginPopupModal("Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        MenuItem item = app.hub().ledger().menu_item(ui.mod_item_id);
        double price = app.hub().ledger().price_for(item, app.happy_hour());
        ImGui::Text("%s  $%.2f", item.name.c_str(), price);
        for (size_t i = 0; i < ui.mod_options.size() && i < 32; ++i) {
            char lab[96];
            std::snprintf(lab, sizeof(lab), "%s (+$%.2f)",
                          ui.mod_options[i].name.c_str(), ui.mod_options[i].price_delta);
            ImGui::Checkbox(lab, &ui.mod_sel[i]);
        }
        ImGui::InputText("Note", ui.line_note, sizeof(ui.line_note));
        if (ImGui::Button("Add", ImVec2(120, 0))) {
            CartLine cl;
            cl.menu_item_id = item.id;
            cl.name = item.name;
            cl.qty = 1;
            cl.unit_price = price;
            cl.note = ui.line_note;
            for (size_t i = 0; i < ui.mod_options.size() && i < 32; ++i) {
                if (ui.mod_sel[i]) {
                    cl.modifier_ids.push_back(ui.mod_options[i].id);
                    cl.modifier_names.push_back(ui.mod_options[i].name);
                    cl.unit_price += ui.mod_options[i].price_delta;
                }
            }
            ui.cart.push_back(cl);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void draw_checkout_popup(PosApp& app, UiState& ui) {
    if (!ui.show_checkout) return;
    ImGui::OpenPopup("Checkout");
    if (ImGui::BeginPopupModal("Checkout", &ui.show_checkout, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto sess = app.session_for_table(ui.selected_table);
        double due = sess ? session_due(*sess) : 0;
        double taxable = std::max(0.0, due - ui.pay_discount);
        double tax = taxable * 0.10;
        double total = taxable + tax + ui.pay_tip;

        ImGui::Text("Table T%d  %s", ui.selected_table, ui.customer);
        ImGui::Text("Subtotal: $%.2f", due);
        ImGui::InputDouble("Discount $", &ui.pay_discount, 0.5, 1.0, "%.2f");
        if (!app.is_manager() && ui.pay_discount > due * 0.15) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Large discount — manager recommended");
        }
        ImGui::InputDouble("Tip $", &ui.pay_tip, 0.5, 1.0, "%.2f");
        ImGui::RadioButton("Cash", &ui.pay_method, 0); ImGui::SameLine();
        ImGui::RadioButton("Card", &ui.pay_method, 1);
        ImGui::Separator();
        ImGui::Text("Tax 10%%: $%.2f", tax);
        ImGui::Text("TOTAL: $%.2f", total);
        ImGui::Spacing();
        if (ImGui::Button("Take payment & print receipt", ImVec2(320, 48))) {
            if (ui.selected_session && do_pay(app, ui, ui.selected_session, ui.selected_table)) {
                ImGui::CloseCurrentPopup();
                ui.show_checkout = false;
            }
        }
        if (ImGui::Button("Cancel", ImVec2(320, 0))) {
            ui.show_checkout = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ===================== WAITER =====================
void draw_waiter_service(PosApp& app, UiState& ui) {
    ImGui::Text("Waiter service — open table → order → receipt → free table");
    ImGui::Separator();
    draw_table_tiles(app, ui, 200.f);

    auto tables = app.tables();
    auto cur = find_table(tables, ui.selected_table);

    ImGui::BeginChild("waiter_main", ImVec2(0, 0), true);

    // Left: table actions
    ImGui::BeginChild("waiter_left", ImVec2(280, 0), true);
    ImGui::Text("Selected: %s", cur.label.empty() ? "—" : cur.label.c_str());
    ImGui::TextColored(status_color(cur.status), "%s", to_string(cur.status));
    ImGui::Text("Total: $%.2f", cur.running_total);
    ImGui::Separator();

    if (cur.status == TableStatus::Free || cur.status == TableStatus::Dirty) {
        if (cur.status == TableStatus::Dirty) {
            ImGui::TextWrapped("Table is dirty. Free it after cleaning, then seat guests.");
            if (ImGui::Button("Free table", ImVec2(-1, 52))) {
                app.mark_table_clean(cur.id);
                set_msg(ui, cur.label + " is free");
                ui.selected_session = 0;
            }
            ImGui::Separator();
        }
        ImGui::Text("Seat guests");
        ImGui::InputText("Name", ui.customer, sizeof(ui.customer));
        ImGui::InputInt("Guests", &ui.covers);
        if (ui.covers < 1) ui.covers = 1;
        if (ImGui::Button("Open table", ImVec2(-1, 48))) {
            if (cur.status == TableStatus::Dirty) app.mark_table_clean(cur.id);
            int sid = app.open_table(cur.id, ui.covers, ui.customer);
            ui.selected_session = sid;
            set_msg(ui, "Opened " + cur.label + " — add orders");
        }
    } else {
        ui.selected_session = cur.open_session_id;
        ImGui::TextWrapped("Guest: %s\nCovers: %d\nSession #%d",
                           cur.customer_name.c_str(), cur.covers, cur.open_session_id);
        ImGui::Spacing();
        if (ImGui::Button("Request bill", ImVec2(-1, 40))) {
            app.request_bill(cur.id);
            ui.show_checkout = true;
            set_msg(ui, "Bill requested");
        }
        if (ImGui::Button("Checkout / Receipt", ImVec2(-1, 48))) {
            ui.show_checkout = true;
        }
        if (cur.status == TableStatus::Dirty || !cur.open_session_id) {
            // after pay table is dirty
        }
        // Allow free only when dirty (after payment) or manager override - waiter frees dirty
        if (cur.status == TableStatus::Dirty) {
            if (ImGui::Button("Free table", ImVec2(-1, 52))) {
                app.mark_table_clean(cur.id);
                set_msg(ui, cur.label + " free again");
            }
        }
    }

    if (!ui.last_receipt_no.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 1, 0.6f, 1), "Last receipt");
        ImGui::Text("%s", ui.last_receipt_no.c_str());
        ImGui::Text("$%.2f", ui.last_receipt_total);
        if (ImGui::Button("Free table now", ImVec2(-1, 44))) {
            app.mark_table_clean(ui.selected_table);
            ui.last_receipt_no.clear();
            set_msg(ui, "Table freed — ready for next guests");
        }
    }

    ImGui::EndChild();
    ImGui::SameLine();

    // Right: order entry when session open
    ImGui::BeginChild("waiter_right", ImVec2(0, 0), true);
    if (cur.open_session_id) {
        ui.selected_session = cur.open_session_id;
        draw_order_panel(app, ui, true);
    } else if (cur.status == TableStatus::Dirty) {
        ImGui::TextWrapped("Payment done or table needs cleaning.\nPress Free table on the left.");
        if (!ui.receipt_text.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted(ui.receipt_text.c_str());
        }
    } else {
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::TextDisabled("Select a free table and open it to start ordering.");
    }
    ImGui::EndChild();

    ImGui::EndChild();
    draw_checkout_popup(app, ui);
}

void draw_receipts_list(PosApp& app, UiState& ui, bool manager_void) {
    ImGui::Text("Receipts");
    auto list = app.receipts();
    ImGui::BeginChild("rlist", ImVec2(340, 0), true);
    for (const auto& r : list) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s  $%.2f  %s", r.receipt_no.c_str(), r.total,
                      r.status == ReceiptStatus::Void ? "VOID" : "OK");
        if (ImGui::Selectable(buf, ui.selected_receipt == r.id)) {
            ui.selected_receipt = r.id;
            format_receipt(r, ui.receipt_text);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("rview", ImVec2(0, 0), true);
    ImGui::TextUnformatted(ui.receipt_text.empty() ? "Select a receipt" : ui.receipt_text.c_str());
    if (ui.selected_receipt) {
        if (ImGui::Button("Reprint")) {
            app.reprint(ui.selected_receipt);
            set_msg(ui, "Reprint counted");
            if (auto r = app.hub().ledger().receipt(ui.selected_receipt)) format_receipt(*r, ui.receipt_text);
        }
        if (manager_void) {
            ImGui::SameLine();
            if (ImGui::Button("Void receipt")) {
                if (app.void_receipt(ui.selected_receipt, "manager void")) set_msg(ui, "Receipt voided");
                else set_msg(ui, "Void failed");
            }
        }
    }
    ImGui::EndChild();
}

// ===================== MANAGER =====================
void draw_manager_floor(PosApp& app, UiState& ui) {
    ImGui::Text("Floor overview (manager)");
    draw_table_tiles(app, ui, 220.f);
    auto cur = find_table(app.tables(), ui.selected_table);
    ImGui::Text("Selected %s — %s — session %d — $%.2f",
                cur.label.c_str(), to_string(cur.status), cur.open_session_id, cur.running_total);
    if (cur.open_session_id) {
        if (auto sess = app.session_for_table(cur.id)) {
            for (const auto& o : sess->orders) {
                ImGui::BulletText("Order #%d [%s]", o.id, to_string(o.status));
                ImGui::SameLine();
                if (ImGui::SmallButton(("Void##" + std::to_string(o.id)).c_str())) {
                    if (app.void_order(o.id, "manager void")) set_msg(ui, "Order voided + restocked");
                    else set_msg(ui, "Void failed");
                }
            }
        }
    }
    if (ImGui::Button("Force free table")) {
        app.mark_table_clean(cur.id);
        set_msg(ui, "Table forced free");
    }
    ImGui::SameLine();
    if (ImGui::Button("Open as manager service") && (cur.status == TableStatus::Free || cur.status == TableStatus::Dirty)) {
        if (cur.status == TableStatus::Dirty) app.mark_table_clean(cur.id);
        int sid = app.open_table(cur.id, ui.covers, ui.customer[0] ? ui.customer : "Manager");
        ui.selected_session = sid;
        set_msg(ui, "Session opened");
    }
}

void draw_kitchen(PosApp& app, UiState& ui) {
    ImGui::Text("Kitchen / Bar tickets");
    auto tickets = app.kitchen_tickets();
    if (tickets.empty()) ImGui::TextDisabled("No open tickets — kitchen is clear.");
    float x = 0;
    for (const auto& t : tickets) {
        ImGui::PushID(t.id);
        ImVec4 col = t.station == TicketStation::Bar
                         ? ImVec4(0.2f, 0.45f, 0.85f, 1)
                         : ImVec4(0.85f, 0.45f, 0.15f, 1);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(col.x * 0.22f, col.y * 0.22f, col.z * 0.22f, 1));
        ImGui::BeginChild(("tk" + std::to_string(t.id)).c_str(), ImVec2(260, 170), true);
        ImGui::Text("%s | %s | #%d %s",
                    t.table_label.c_str(),
                    t.station == TicketStation::Bar ? "BAR" : "KITCHEN",
                    t.order_id, t.priority ? "RUSH" : "");
        ImGui::TextDisabled("%s", t.created_at.c_str());
        for (const auto& l : t.lines)
            ImGui::BulletText("%dx %s", l.qty, l.name.c_str());
        if (t.status == TicketStatus::Queued) {
            if (ImGui::Button("Start", ImVec2(-1, 32))) {
                app.ticket_start(t.id);
                set_msg(ui, "Ticket started");
            }
        } else if (t.status == TicketStatus::InProgress) {
            if (ImGui::Button("Done / Ready", ImVec2(-1, 32))) {
                app.ticket_done(t.id);
                set_msg(ui, "Ticket done");
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        x += 270;
        if (x + 270 < ImGui::GetContentRegionAvail().x) ImGui::SameLine();
        else x = 0;
        ImGui::PopID();
    }
}

void draw_stock(PosApp& app, UiState& ui) {
    ImGui::Text("Stock management");
    auto ings = app.stock();
    ImGui::BeginChild("stocklist", ImVec2(0, ImGui::GetContentRegionAvail().y - 90), true);
    if (ImGui::BeginTable("stock", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Qty");
        ImGui::TableSetupColumn("Unit");
        ImGui::TableSetupColumn("Reorder");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();
        for (const auto& i : ings) {
            ImGui::TableNextRow();
            if (i.low()) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(120, 40, 40, 120));
            ImGui::TableNextColumn();
            if (ImGui::Selectable(i.name.c_str(), ui.stock_sel == i.id, ImGuiSelectableFlags_SpanAllColumns))
                ui.stock_sel = i.id;
            ImGui::TableNextColumn(); ImGui::Text("%.1f", i.stock_qty);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(i.unit.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%.1f", i.reorder_level);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(i.low() ? "LOW" : "ok");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::InputFloat("Qty", &ui.receive_qty);
    ImGui::SameLine();
    ImGui::InputText("Note", ui.stock_note, sizeof(ui.stock_note));
    if (ImGui::Button("Receive delivery")) {
        if (ui.stock_sel) { app.receive(ui.stock_sel, ui.receive_qty, ui.stock_note); set_msg(ui, "Stock received"); }
        else set_msg(ui, "Select an ingredient");
    }
    ImGui::SameLine();
    if (ImGui::Button("Log waste")) {
        if (ui.stock_sel) { app.waste(ui.stock_sel, ui.receive_qty, ui.stock_note); set_msg(ui, "Waste logged"); }
    }
}

void draw_products(PosApp& app, UiState& ui) {
    ImGui::Text("Product catalog — add / enable / disable");
    auto cats = app.hub().ledger().categories();
    auto ings = app.stock();
    auto all_menu = app.hub().ledger().menu_items(false);

    ImGui::BeginChild("prod_form", ImVec2(ImGui::GetContentRegionAvail().x * 0.42f, 0), true);
    ImGui::Text("Add new product");
    ImGui::InputText("Name", ui.prod_name, sizeof(ui.prod_name));
    ImGui::InputFloat("Price $", &ui.prod_price);
    if (ImGui::BeginCombo("Category", cats.empty() ? "-" : cats[std::min(ui.prod_cat_idx, (int)cats.size() - 1)].second.c_str())) {
        for (int i = 0; i < (int)cats.size(); ++i)
            if (ImGui::Selectable(cats[i].second.c_str(), ui.prod_cat_idx == i)) ui.prod_cat_idx = i;
        ImGui::EndCombo();
    }
    ImGui::RadioButton("Food", &ui.prod_type, 0); ImGui::SameLine();
    ImGui::RadioButton("Drink", &ui.prod_type, 1);
    ImGui::InputText("Size label", ui.prod_size, sizeof(ui.prod_size));
    ImGui::InputText("Allergens", ui.prod_allergens, sizeof(ui.prod_allergens));
    ImGui::Separator();
    ImGui::Text("Optional recipe (stock deduct)");
    const char* ing_preview = "None";
    std::string ing_preview_owned;
    for (const auto& i : ings) if (i.id == ui.prod_ing_id) { ing_preview_owned = i.name; ing_preview = ing_preview_owned.c_str(); break; }
    if (ImGui::BeginCombo("Ingredient", ing_preview)) {
        if (ImGui::Selectable("None", ui.prod_ing_id == 0)) ui.prod_ing_id = 0;
        for (const auto& i : ings)
            if (ImGui::Selectable(i.name.c_str(), ui.prod_ing_id == i.id)) ui.prod_ing_id = i.id;
        ImGui::EndCombo();
    }
    ImGui::InputFloat("Qty per serving", &ui.prod_ing_qty);

    if (ImGui::Button("Save product", ImVec2(-1, 44))) {
        if (!cats.empty() && ui.prod_name[0]) {
            Ledger::NewProductInput in;
            in.category_id = cats[std::min(ui.prod_cat_idx, (int)cats.size() - 1)].first;
            in.name = ui.prod_name;
            in.base_price = ui.prod_price;
            in.item_type = ui.prod_type == 1 ? "drink" : "food";
            in.size_label = ui.prod_size;
            in.allergens = ui.prod_allergens;
            in.recipe_ingredient_id = ui.prod_ing_id;
            in.recipe_qty = ui.prod_ing_qty;
            auto res = app.add_product(in);
            if (res.ok) {
                set_msg(ui, "Product added id=" + std::to_string(res.menu_item_id));
                ui.prod_name[0] = 0;
            } else set_msg(ui, res.error);
        } else set_msg(ui, "Name and category required");
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("prod_list", ImVec2(0, 0), true);
    ImGui::Text("All products (%d)", (int)all_menu.size());
    for (const auto& m : all_menu) {
        ImGui::PushID(m.id);
        bool on = m.available;
        if (ImGui::Checkbox("##av", &on)) {
            app.set_product_available(m.id, on);
            set_msg(ui, std::string(on ? "Enabled " : "Disabled ") + m.name);
        }
        ImGui::SameLine();
        ImGui::Text("%s  $%.2f  [%s]%s", m.name.c_str(), m.base_price, m.category.c_str(),
                    m.available ? "" : "  OFF");
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_report(PosApp& app, UiState& ui) {
    auto r = app.report_today();
    ImGui::Text("Daily report — %s", r.day.c_str());
    ImGui::Separator();
    ImGui::Text("Receipts: %d", r.receipts);
    ImGui::Text("Revenue: $%.2f", r.revenue);
    ImGui::Text("Tax: $%.2f", r.tax);
    ImGui::Text("Tips: $%.2f", r.tips);
    ImGui::Text("Covers: %d", r.covers);
    ImGui::Text("Voids: %d", r.voids);
    ImGui::Separator();
    ImGui::Text("Top sellers");
    for (auto& t : r.top_sellers) ImGui::BulletText("%s — %d", t.first.c_str(), t.second);
    ImGui::Separator();
    ImGui::Text("Ingredient usage");
    for (auto& u : r.ingredient_usage) ImGui::BulletText("%s — %.1f", u.first.c_str(), u.second);
    (void)ui;
}

void draw_login_v2(PosApp& app, UiState& ui) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

    const float box_w = 480.f;
    ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - box_w) * 0.5f, 90.f));
    ImGui::BeginChild("loginbox", ImVec2(box_w, 380), true);

    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("RestoPulse");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Two sessions only");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("mcard", ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, 110), true);
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1), "MANAGER");
    ImGui::TextWrapped("Kitchen, stock, products, floor, reports");
    ImGui::Text("PIN  0000");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("wcard", ImVec2(0, 110), true);
    ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1), "WAITER");
    ImGui::TextWrapped("Tables → orders → receipt → free");
    ImGui::Text("PIN  1111 / 2222");
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::PushItemWidth(-1);
    bool enter = ImGui::InputTextWithHint("##pin", "PIN code", ui.pin, sizeof(ui.pin),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();

    bool go = ImGui::Button("Start session", ImVec2(-1, 50)) || enter;
    if (go) {
        if (app.login(ui.pin)) {
            ui.tab = 0;
            ui.cart.clear();
            ui.show_checkout = false;
            ui.last_receipt_no.clear();
            set_msg(ui, std::string("Welcome ") + app.current().name +
                            (app.is_manager() ? " — Manager console" : " — Waiter service"));
        } else {
            set_msg(ui, "Invalid PIN (manager or waiter only)");
        }
        ui.pin[0] = 0;
    }
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1, 0.85f, 0.35f, 1), "%s", ui.status_msg);
    ImGui::EndChild();
    ImGui::End();
}

} // namespace

int run_ui(PosApp& app, const char* title) {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(1440, 920, title, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 8.f;
    style.WindowRounding = 10.f;
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(10, 8);
    style.GrabRounding = 6.f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    UiState ui;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (!app.logged_in()) {
            draw_login_v2(app, ui);
        } else {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("RestoPulse", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoBringToFrontOnFocus);

            // Top bar
            if (app.is_manager())
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1), "MANAGER");
            else
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1), "WAITER");
            ImGui::SameLine();
            ImGui::Text("· %s", app.current().name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", app.status_line().c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 110);
            if (ImGui::Button("Logout", ImVec2(100, 0))) {
                app.logout();
                ui = UiState{};
            }
            ImGui::Separator();

            if (app.is_waiter()) {
                if (ImGui::BeginTabBar("waiter_tabs")) {
                    if (ImGui::BeginTabItem("Service")) {
                        ui.tab = 0;
                        draw_waiter_service(app, ui);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("My receipts")) {
                        ui.tab = 1;
                        draw_receipts_list(app, ui, false);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            } else {
                // Manager full console
                if (ImGui::BeginTabBar("mgr_tabs")) {
                    if (ImGui::BeginTabItem("Floor")) {
                        ui.tab = 0;
                        draw_manager_floor(app, ui);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Kitchen")) {
                        ui.tab = 1;
                        draw_kitchen(app, ui);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Stock")) {
                        ui.tab = 2;
                        draw_stock(app, ui);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Products")) {
                        ui.tab = 3;
                        draw_products(app, ui);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Receipts")) {
                        ui.tab = 4;
                        draw_receipts_list(app, ui, true);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Reports")) {
                        ui.tab = 5;
                        draw_report(app, ui);
                        ImGui::EndTabItem();
                    }
                    // Optional: manager can also take orders on a table
                    if (ImGui::BeginTabItem("Take order")) {
                        ui.tab = 6;
                        draw_table_tiles(app, ui, 160.f);
                        auto cur = find_table(app.tables(), ui.selected_table);
                        if (cur.open_session_id) {
                            ui.selected_session = cur.open_session_id;
                            draw_order_panel(app, ui, true);
                            draw_checkout_popup(app, ui);
                        } else {
                            ImGui::TextDisabled("Select an open table, or open one from Floor.");
                            ImGui::InputText("Customer", ui.customer, sizeof(ui.customer));
                            ImGui::InputInt("Covers", &ui.covers);
                            if (ImGui::Button("Open selected table") &&
                                (cur.status == TableStatus::Free || cur.status == TableStatus::Dirty)) {
                                if (cur.status == TableStatus::Dirty) app.mark_table_clean(cur.id);
                                ui.selected_session = app.open_table(cur.id, ui.covers, ui.customer);
                            }
                        }
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.7f, 0.92f, 1.f, 1), "%s", ui.status_msg);
            ImGui::End();
        }

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.07f, 0.08f, 0.10f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace rp
