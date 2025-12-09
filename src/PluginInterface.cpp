#include "PluginInterface.hpp"

#include <iomanip>

extern "C" void AboutReport(rapidjson::Value& request,
                            rapidjson::Value& response,
                            rapidjson::Document::AllocatorType& allocator,
                            CServerInterface* server) {
    response.AddMember("version", 1, allocator);
    response.AddMember("name", Value().SetString("Credit Facility report", allocator), allocator);
    response.AddMember("description",
    Value().SetString("Displays credit operations on client accounts, covering both incoming and outgoing transactions. "
                        "Includes operation IDs, dates, amounts, and trader information.",
             allocator), allocator);
    response.AddMember("type", REPORT_RANGE_GROUP_TYPE, allocator);
}

extern "C" void DestroyReport() {}

extern "C" void CreateReport(rapidjson::Value& request,
                             rapidjson::Value& response,
                             rapidjson::Document::AllocatorType& allocator,
                             CServerInterface* server) {
    std::string group_mask;
    int from;
    int to;
    if (request.HasMember("group") && request["group"].IsString()) {
        group_mask = request["group"].GetString();
    }
    if (request.HasMember("from") && request["from"].IsNumber()) {
        from = request["from"].GetInt();
    }
    if (request.HasMember("to") && request["to"].IsNumber()) {
        to = request["to"].GetInt();
    }

    std::vector<TradeRecord> trades_vector;
    std::vector<GroupRecord> groups_vector;
    double usd_total_profit = 0;

    try {
        server->GetTransactionsByGroup(group_mask, from, to, &trades_vector);
        server->GetAllGroups(&groups_vector);
    } catch (const std::exception& e) {
        std::cerr << "[CreditFacilityReportInterface]: " << e.what() << std::endl;
    }

    // Лямбда для поиска валюты аккаунта по группе
    auto get_group_currency = [&](const std::string& group_name) -> std::string {
        for (const auto& group : groups_vector) {
            if (group.group == group_name) {
                return group.currency;
            }
        }
        return "N/A"; // группа не найдена - валюта не определена
    };

    // Лямбда подготавливающая значения double для вставки в AST (округление до 2-х знаков)
    auto format_double_for_AST = [](double value) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        return oss.str();
    };

    TableBuilder table_builder("CreditFacilityReport");

    table_builder.SetIdColumn("order");
    table_builder.SetOrderBy("order", "DESC");
    table_builder.EnableRefreshButton(false);
    table_builder.EnableBookmarksButton(false);
    table_builder.EnableExportButton(true);
    table_builder.EnableTotal(true);

    table_builder.AddColumn({"order", "ORDER"});
    table_builder.AddColumn({"login", "LOGIN"});
    table_builder.AddColumn({"name", "NAME"});
    table_builder.AddColumn({"close_time", "CLOSE_TIME"});
    table_builder.AddColumn({"comment", "COMMENT"});
    table_builder.AddColumn({"profit", "AMOUNT"});
    table_builder.AddColumn({"currency", "CURRENCY"});

    for (const auto& trade : trades_vector) {
        if (trade.cmd == OP_CREDIT_IN || trade.cmd == OP_CREDIT_OUT) {
            AccountRecord account;

            try {
                server->GetAccountByLogin(trade.login, &account);
            } catch (const std::exception& e) {
                std::cerr << "[CreditFacilityReportInterface]: " << e.what() << std::endl;
            }

            std::string currency = get_group_currency(account.group);

            if (currency == "USD") {
                usd_total_profit += trade.profit;
            } else {
                double multiplier;

                try {
                    server->CalculateConvertRateByCurrency(currency, "USD", trade.cmd, &multiplier);
                } catch (const std::exception& e) {
                    std::cerr << "[CreditFacilityReportInterface]: " << e.what() << std::endl;
                }

                usd_total_profit += trade.profit * multiplier;
            }

            table_builder.AddRow({
                {"order", std::to_string(trade.order)},
                {"login", std::to_string(trade.login)},
                {"name", account.name},
                {"close_time", utils::FormatTimestampToString(trade.close_time)},
                {"comment", trade.comment},
                {"profit", format_double_for_AST(trade.profit)},
                {"currency", currency}
            });
        }
    }

    // Total row
    JSONArray totals_array;
    totals_array.emplace_back(JSONObject{{"profit", usd_total_profit}});

    table_builder.SetTotalData(totals_array);

    const JSONObject table_props = table_builder.CreateTableProps();
    const Node table_node = Table({}, table_props);

    // Total report
    const Node report = Column({
        h1({text("Credit Facility Report")}),
        table_node
    });

    utils::CreateUI(report, response, allocator);
}