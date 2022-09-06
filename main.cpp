#include <emscripten.h>
#include <arrow/compute/cast.h>
#include <arrow/compute/exec/options.h>
#include <arrow/compute/exec/exec_plan.h>
#include <arrow/compute/exec/tpch_node.h>
#include <arrow/util/make_unique.h>
#include <arrow/util/future.h>
#include <arrow/util/vector.h>

#include <iostream>
#include <chrono>

using namespace arrow::compute;
using namespace arrow;
using arrow::compute::internal::TpchGen;

Future<std::vector<ExecBatch>> StartAndCollect(
    ExecPlan* plan,
    AsyncGenerator<util::optional<ExecBatch>> gen)
{
    std::cout << "A1" << std::endl;
    RETURN_NOT_OK(plan->Validate());
    std::cout << "B1" << std::endl;
    RETURN_NOT_OK(plan->StartProducing());
    std::cout << "C1" << std::endl;
    auto collected_fut = CollectAsyncGenerator(gen);
    
    return AllComplete({ plan->finished(), Future<>(collected_fut) })
        .Then([collected_fut]() -> Result<std::vector<ExecBatch>>
              {
                  ARROW_ASSIGN_OR_RAISE(auto collected, collected_fut.result());
                  return arrow::internal::MapVector(
                      [](util::optional<ExecBatch> batch) { return std::move(*batch); },
                      std::move(collected));
              });
}

std::shared_ptr<ExecPlan> Plan_Q1(
    AsyncGenerator<util::optional<ExecBatch>>* sink_gen,
    int scale_factor)
{
    ExecContext* ctx = default_exec_context();
    *ctx = ExecContext(default_memory_pool(), arrow::internal::GetCpuThreadPool());
    std::shared_ptr<ExecPlan> plan = *ExecPlan::Make(ctx);
    std::unique_ptr<TpchGen> gen =
        *TpchGen::Make(
            plan.get(),
            static_cast<double>(scale_factor));

    ExecNode* lineitem = *gen->Lineitem(
        {
            "L_QUANTITY",
            "L_EXTENDEDPRICE",
            "L_TAX",
            "L_DISCOUNT",
            "L_SHIPDATE",
            "L_RETURNFLAG",
            "L_LINESTATUS"
        });

    auto sept_2_1998 = std::make_shared<Date32Scalar>(10471);  // September 2, 1998 is 10471 days after January 1, 1970
    Expression filter =
        less_equal(field_ref("L_SHIPDATE"), literal(std::move(sept_2_1998)));
    FilterNodeOptions filter_opts(filter);

    Expression l_returnflag = field_ref("L_RETURNFLAG");
    Expression l_linestatus = field_ref("L_LINESTATUS");
    Expression quantity = field_ref("L_QUANTITY");
    Expression base_price = field_ref("L_EXTENDEDPRICE");

    std::shared_ptr<Decimal128Scalar> decimal_1 =
        std::make_shared<Decimal128Scalar>(Decimal128{0, 100}, decimal(12, 2));
    Expression discount_multiplier =
        call("subtract", {literal(decimal_1), field_ref("L_DISCOUNT")});
    Expression tax_multiplier = call("add", { literal(decimal_1), field_ref("L_TAX") });
    Expression disc_price =
        call("multiply", { field_ref("L_EXTENDEDPRICE"), discount_multiplier });
    Expression charge =
        call("multiply",
             {call("cast",
                   {call("multiply", {field_ref("L_EXTENDEDPRICE"), discount_multiplier})},
                   compute::CastOptions::Unsafe(decimal(12, 2))),
              tax_multiplier});
    Expression discount = field_ref("L_DISCOUNT");

    std::vector<Expression> projection_list = {l_returnflag, l_linestatus, quantity,
                                               base_price,   disc_price,   charge,
                                               quantity,     base_price,   discount};
    std::vector<std::string> project_names =
        {
            "l_returnflag",
            "l_linestatus",
            "sum_qty",
            "sum_base_price",
            "sum_disc_price",
            "sum_charge",
            "avg_qty",
            "avg_price",
            "avg_disc"
        };
    ProjectNodeOptions project_opts(std::move(projection_list), std::move(project_names));

    auto sum_opts = std::make_shared<ScalarAggregateOptions>(ScalarAggregateOptions::Defaults());
    auto count_opts = std::make_shared<CountOptions>(CountOptions::CountMode::ALL);
    std::vector<arrow::compute::Aggregate> aggs =
        {
            { "hash_sum", sum_opts, "sum_qty", "sum_qty" },
            { "hash_sum", sum_opts, "sum_base_price", "sum_base_price" },
            { "hash_sum", sum_opts, "sum_disc_price", "sum_disc_price" },
            { "hash_sum", sum_opts, "sum_charge", "sum_charge" },
            { "hash_mean", sum_opts, "avg_qty", "avg_qty" },
            { "hash_mean", sum_opts, "avg_price", "avg_price" },
            { "hash_mean", sum_opts, "avg_disc", "avg_disc" },
            { "hash_count", count_opts, "sum_qty", "count_order" },
        };
    
    std::vector<FieldRef> to_aggregate =
        {
            "sum_qty",
            "sum_base_price",
            "sum_disc_price",
            "sum_charge",
            "avg_qty",
            "avg_price",
            "avg_disc",
            "sum_qty"
        };

    std::vector<std::string> names =
        {
            "sum_qty",
            "sum_base_price",
            "sum_disc_price",
            "sum_charge",
            "avg_qty",
            "avg_price",
            "avg_disc",
            "count_order"
        };

    std::vector<FieldRef> keys = { "l_returnflag", "l_linestatus" };
    AggregateNodeOptions agg_opts(aggs, keys);

    SortKey l_returnflag_key("l_returnflag");
    SortKey l_linestatus_key("l_linestatus");
    SortOptions sort_opts({l_returnflag_key, l_linestatus_key});
    OrderBySinkNodeOptions order_by_opts(sort_opts, sink_gen);

    Declaration filter_decl("filter", {Declaration::Input(lineitem)}, filter_opts);
    Declaration project_decl("project", project_opts);
    Declaration aggregate_decl("aggregate", agg_opts);
    Declaration orderby_decl("order_by_sink", order_by_opts);

    Declaration q1 =
        Declaration::Sequence(
            {
                filter_decl, project_decl, aggregate_decl, orderby_decl
            });
    std::ignore = *q1.AddToPlan(plan.get());
    return plan;
}


extern "C"
int main(int argc, const char **argv)
{
    std::cout << "START!" << std::endl;
    double time = 0;
    constexpr int num_iters = 10;
    for(int i = 0; i < num_iters; i++)
    {
        std::cout << "A!" << std::endl;
        AsyncGenerator<util::optional<ExecBatch>> sink_gen;
        std::cout << "B!" << std::endl;
        std::shared_ptr<ExecPlan> plan = Plan_Q1(&sink_gen, 1);
        std::cout << "C!" << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        std::cout << "D!" << std::endl;
        auto fut = StartAndCollect(plan.get(), sink_gen);
        std::cout << "E!" << std::endl;
        auto res = *fut.MoveResult();
        std::cout << "F!" << std::endl;
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "G!" << std::endl;
        time += (end - start).count();
    }
    std::cout << "TPCH Q1 took " << (time / num_iters) << "sec!\n";
    return 0;
}
