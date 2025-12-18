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

    TableBuilder table_builder("CreditFacilityReport");

    table_builder.SetIdColumn("order");
    table_builder.SetOrderBy("order", "DESC");
    table_builder.EnableRefreshButton(false);
    table_builder.EnableBookmarksButton(false);
    table_builder.EnableExportButton(true);
    table_builder.EnableTotal(true);
    table_builder.SetTotalDataTitle("TOTAL");

    table_builder.AddColumn({"order", "ORDER", 1});
    table_builder.AddColumn({"login", "LOGIN", 2});
    table_builder.AddColumn({"name", "NAME", 3});
    table_builder.AddColumn({"close_time", "CLOSE_TIME", 4});
    table_builder.AddColumn({"comment", "COMMENT", 5});
    table_builder.AddColumn({"profit", "AMOUNT", 6});
    table_builder.AddColumn({"currency", "CURRENCY", 7});

    for (const auto& trade : trades_vector) {
        if (trade.cmd == OP_CREDIT_IN || trade.cmd == OP_CREDIT_OUT) {
            AccountRecord account;

            try {
                server->GetAccountByLogin(trade.login, &account);
            } catch (const std::exception& e) {
                std::cerr << "[CreditFacilityReportInterface]: " << e.what() << std::endl;
            }

            std::string currency = utils::GetGroupCurrencyByName(groups_vector, account.group);
            double multiplier = 1;

            if (currency == "USD") {
                usd_total_profit += trade.profit;
            } else {

                try {
                    server->CalculateConvertRateByCurrency(currency, "USD", trade.cmd, &multiplier);
                } catch (const std::exception& e) {
                    std::cerr << "[CreditFacilityReportInterface]: " << e.what() << std::endl;
                }

                usd_total_profit += trade.profit * multiplier;
            }

            table_builder.AddRow({
                utils::TruncateDouble(trade.order, 0),
                utils::TruncateDouble(trade.login, 0),
                account.name,
                utils::FormatTimestampToString(trade.close_time),
                trade.comment,
                utils::TruncateDouble(trade.profit * multiplier, 2),
                "USD"
            });
        }
    }

    // Total row
    JSONArray totals_array;
    totals_array.emplace_back(JSONObject{{"profit", utils::TruncateDouble(usd_total_profit, 2)}, {"currency", "USD"}});

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