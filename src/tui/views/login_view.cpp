#include "login_view.hpp"
#include <ftxui/dom/elements.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateLoginView(TuiContext& ctx) {
    auto input_url = ftxui::Input(&ctx.login_url, "https://...");
    auto input_username = ftxui::Input(&ctx.login_username, "Username (CPF)");
    
    ftxui::InputOption password_opt = ftxui::InputOption::Default();
    password_opt.password = true;
    auto input_password = ftxui::Input(&ctx.login_password, "Password", password_opt);

    auto chk_perm_token = ftxui::Checkbox("Extract Permanent Mobile Token", &ctx.extract_permanent_token);
    auto chk_save_keyring = ftxui::Checkbox("Save Credentials in Keyring", &ctx.save_keyring);

    auto btn_login = ftxui::Button("Login", [&ctx]() {
        if (ctx.perform_login_cb) ctx.perform_login_cb();
    }, ctx.make_button_option(true));

    auto btn_login_exit = ftxui::Button("Exit", [&ctx]() {
        if (ctx.exit_cb) ctx.exit_cb();
    }, ctx.make_button_option(false));

    auto login_container = ftxui::Container::Vertical({
        input_url,
        input_username,
        input_password,
        chk_perm_token,
        chk_save_keyring,
        btn_login,
        btn_login_exit
    });

    return ftxui::Renderer(login_container, [login_container, input_url, input_username, input_password, chk_perm_token, chk_save_keyring, btn_login, btn_login_exit, &ctx]() {
        auto login_box = ftxui::window(ftxui::text(" Moodle Storage Authentication ") | ftxui::bold | ftxui::color(ctx.theme.title), 
            ftxui::vbox({
                ftxui::hbox(ftxui::text("Moodle URL: ") | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 12), input_url->Render() | ftxui::border | ftxui::flex),
                ftxui::hbox(ftxui::text("Username:   ") | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 12), input_username->Render() | ftxui::border | ftxui::flex),
                ftxui::hbox(ftxui::text("Password:   ") | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 12), input_password->Render() | ftxui::border | ftxui::flex),
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                chk_perm_token->Render(),
                chk_save_keyring->Render(),
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::hbox({
                    btn_login->Render(),
                    ftxui::text("   "),
                    btn_login_exit->Render()
                }) | ftxui::center,
                ftxui::text(ctx.login_status) | ftxui::color(ctx.theme.progress_high) | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 60) | ftxui::center;
        
        return ftxui::vbox({
            ftxui::text(" 󰊄 Moodle Storage ") | ftxui::bold | ftxui::color(ctx.theme.title) | ftxui::bgcolor(ctx.theme.selected_bg),
            ftxui::filler(),
            login_box,
            ftxui::filler()
        }) | ftxui::bgcolor(ctx.theme.main_bg);
    });
}

} // namespace mstorage::tui::views
