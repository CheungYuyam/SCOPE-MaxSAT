#ifndef _HEURISTIC_H_
#define _HEURISTIC_H_

#include "basis_pms.h"
#include "deci.h"

void ISDist::init(vector<int> &init_solution)
{
    soft_large_weight_clauses_count = 0;
    if (1 == problem_weighted) // weighted partial MaxSAT
    {
        if (0 != num_hclauses)
        {
            if ((0 == local_soln_feasible || 0 == best_soln_feasible))
            {
                for (int c = 0; c < num_clauses; c++)
                {
                    already_in_soft_large_weight_stack[c] = 0;
                    clause_selected_count[c] = 0;
                    clause_weight[c] = 1;
                }
            }
            else
            {
                for (int c = 0; c < num_clauses; c++)
                {
                    already_in_soft_large_weight_stack[c] = 0;
                    clause_selected_count[c] = 0;

                    if (org_clause_weight[c] == top_clause_weight)
                        clause_weight[c] = 1;
                    else
                    {
                        clause_weight[c] = tuned_org_clause_weight[c];
                        if (clause_weight[c] > s_inc && already_in_soft_large_weight_stack[c] == 0)
                        {
                            already_in_soft_large_weight_stack[c] = 1;
                            soft_large_weight_clauses[soft_large_weight_clauses_count++] = c;
                        }
                    }
                }
            }
        }
        else
        {
            for (int c = 0; c < num_clauses; c++)
            {
                already_in_soft_large_weight_stack[c] = 0;
                clause_selected_count[c] = 0;
                clause_weight[c] = tuned_org_clause_weight[c];
                if (clause_weight[c] > s_inc && already_in_soft_large_weight_stack[c] == 0)
                {
                    already_in_soft_large_weight_stack[c] = 1;
                    soft_large_weight_clauses[soft_large_weight_clauses_count++] = c;
                }
            }
        }
    }
    else // unweighted partial MaxSAT
    {
        for (int c = 0; c < num_clauses; c++)
        {
            already_in_soft_large_weight_stack[c] = 0;
            clause_selected_count[c] = 0;

            if (org_clause_weight[c] == top_clause_weight)
                clause_weight[c] = 1;
            else
            {
                if ((0 == local_soln_feasible || 0 == best_soln_feasible) && num_hclauses > 0)
                {
                    clause_weight[c] = 1;
                }
                else
                {
                    clause_weight[c] = coe_soft_clause_weight;
                    if (clause_weight[c] > 1 && already_in_soft_large_weight_stack[c] == 0)
                    {
                        already_in_soft_large_weight_stack[c] = 1;
                        soft_large_weight_clauses[soft_large_weight_clauses_count++] = c;
                    }
                }
            }
        }
    }

    if (init_solution.size() == 0)
    {
        for (int v = 1; v <= num_vars; v++)
        {
            cur_soln[v] = rand() % 2;
            time_stamp[v] = 0;
            unsat_app_count[v] = 0;
        }
    }
    else
    {
        for (int v = 1; v <= num_vars; v++)
        {
            cur_soln[v] = init_solution[v];
            if (cur_soln[v] != 0 && cur_soln[v] != 1)
                cur_soln[v] = rand() % 2;
            time_stamp[v] = 0;
            unsat_app_count[v] = 0;
        }
    }
    local_soln_feasible = 0;
    // init stacks
    hard_unsat_nb = 0;
    soft_unsat_weight = 0;
    hardunsat_stack_fill_pointer = 0;
    softunsat_stack_fill_pointer = 0;
    unsatvar_stack_fill_pointer = 0;
    large_weight_clauses_count = 0;

    /* figure out sat_count, sat_var and init unsat_stack */
    for (int c = 0; c < num_clauses; ++c)
    {
        sat_count[c] = 0;
        for (int j = 0; j < clause_lit_count[c]; ++j)
        {
            if (cur_soln[clause_lit[c][j].var_num] == clause_lit[c][j].sense)
            {
                sat_count[c]++;
                sat_var[c] = clause_lit[c][j].var_num;
            }
        }
        if (sat_count[c] == 0)
        {
            unsat(c);
        }
    }

    /*figure out score*/
    for (int v = 1; v <= num_vars; v++)
    {
        score[v] = 0.0;
        for (int i = 0; i < var_lit_count[v]; ++i)
        {
            int c = var_lit[v][i].clause_num;
            if (sat_count[c] == 0)
                score[v] += clause_weight[c];
            else if (sat_count[c] == 1 && var_lit[v][i].sense == cur_soln[v])
                score[v] -= clause_weight[c];
        }
    }

    // init goodvars stack
    goodvar_stack_fill_pointer = 0;
    for (int v = 1; v <= num_vars; v++)
    {
        if (score[v] > 0)
        {
            already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
            mypush(v, goodvar_stack);
        }
        else
            already_in_goodvar_stack[v] = -1;
    }

#ifdef NUWLS_QUALITY_SEARCH
    if (quality_phase_active)
        apply_quality_weights();
#endif
}

#ifdef NUWLS_QUALITY_SEARCH
void ISDist::rebuild_scores_and_goodvars()
{
    for (int v = 1; v <= num_vars; ++v)
    {
        score[v] = 0.0;
        for (int i = 0; i < var_lit_count[v]; ++i)
        {
            const int c = var_lit[v][i].clause_num;
            if (sat_count[c] == 0)
                score[v] += clause_weight[c];
            else if (sat_count[c] == 1 && var_lit[v][i].sense == cur_soln[v])
                score[v] -= clause_weight[c];
        }
    }

    goodvar_stack_fill_pointer = 0;
    for (int v = 1; v <= num_vars; ++v)
    {
        if (score[v] > 0)
        {
            already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
            mypush(v, goodvar_stack);
        }
        else
            already_in_goodvar_stack[v] = -1;
    }
}

void ISDist::apply_quality_weights()
{
    soft_large_weight_clauses_count = 0;
    for (int c = 0; c < num_clauses; ++c)
    {
        already_in_soft_large_weight_stack[c] = 0;
        if (org_clause_weight[c] != top_clause_weight)
        {
            clause_weight[c] = problem_weighted
                ? tuned_org_clause_weight[c]
                : static_cast<double>(coe_soft_clause_weight);
            if (clause_weight[c] > s_inc)
            {
                already_in_soft_large_weight_stack[c] = 1;
                soft_large_weight_clauses[soft_large_weight_clauses_count++] = c;
            }
        }
    }

    if (quality_hard_guard_factor > 0.0 && num_hclauses > 0)
    {
        vector<double> incident_soft_weight(static_cast<size_t>(num_vars) + 1, 0.0);
        double max_incident_soft_weight = 0.0;
        for (int c = 0; c < num_clauses; ++c)
        {
            if (org_clause_weight[c] == top_clause_weight)
                continue;
            for (lit *p = clause_lit[c]; p->var_num != 0; ++p)
                incident_soft_weight[p->var_num] += clause_weight[c];
        }
        for (int v = 1; v <= num_vars; ++v)
            if (incident_soft_weight[v] > max_incident_soft_weight)
                max_incident_soft_weight = incident_soft_weight[v];

        const double guarded_hard_weight =
            quality_hard_guard_factor * max_incident_soft_weight + h_inc;
        for (int c = 0; c < num_clauses; ++c)
            if (org_clause_weight[c] == top_clause_weight &&
                clause_weight[c] < guarded_hard_weight)
                clause_weight[c] = guarded_hard_weight;
    }

    rebuild_scores_and_goodvars();
}
#endif

int ISDist::pick_var()
{
    int i, v;
    int best_var;
    int sel_c;
    lit *p;

    if (goodvar_stack_fill_pointer > 0)
    {
        int best_array_count = 0;
        if ((rand() % MY_RAND_MAX_INT) * BASIC_SCALE < rdprob)
            return goodvar_stack[rand() % goodvar_stack_fill_pointer];

        if (goodvar_stack_fill_pointer < hd_count_threshold)
        {
            best_var = goodvar_stack[0];

            for (i = 1; i < goodvar_stack_fill_pointer; ++i)
            {
                v = goodvar_stack[i];
                if (score[v] > score[best_var])
                {
                    best_var = v;
                }
                else if (score[v] == score[best_var])
                {
                    if (time_stamp[v] < time_stamp[best_var])
                    {
                        best_var = v;
                    }
                }
            }
            return best_var; // best_array[rand() % best_array_count];
        }
        else
        {
            best_var = goodvar_stack[rand() % goodvar_stack_fill_pointer];

            for (i = 1; i < hd_count_threshold; ++i)
            {
                v = goodvar_stack[rand() % goodvar_stack_fill_pointer];
                if (score[v] > score[best_var])
                {
                    best_var = v;
                }
                else if (score[v] == score[best_var])
                {
                    if (time_stamp[v] < time_stamp[best_var])
                    {
                        best_var = v;
                    }
                }
            }
            return best_var; // best_array[rand() % best_array_count];
        }
    }

    update_clause_weights();

    if (hardunsat_stack_fill_pointer > 0)
    {
        sel_c = hardunsat_stack[rand() % hardunsat_stack_fill_pointer];
    }
    else
    {
        sel_c = softunsat_stack[rand() % softunsat_stack_fill_pointer];
    }
    if ((rand() % MY_RAND_MAX_INT) * BASIC_SCALE < rwprob)
        return clause_lit[sel_c][rand() % clause_lit_count[sel_c]].var_num;

    best_var = clause_lit[sel_c][0].var_num;
    p = clause_lit[sel_c];
    for (p++; (v = p->var_num) != 0; p++)
    {
        if (score[v] > score[best_var])
            best_var = v;
        else if (score[v] == score[best_var])
        {
            if (time_stamp[v] < time_stamp[best_var])
                best_var = v;
        }
    }

    return best_var;
}

void ISDist::local_search_with_decimation(char *inputfile)
{
    if (1 == problem_weighted)
    {
        if (total_soft_length / num_sclauses > 100)
        {
            //cout << "c avg_soft_length: " << total_soft_length / num_sclauses << endl;
            h_inc = 300;
            s_inc = 100;
        }
        if (0 != num_hclauses)
        {
#ifdef NUWLS_QUALITY_SEARCH
            const double mean_soft_weight = static_cast<double>(total_soft_weight) /
                static_cast<double>(num_sclauses);
            double transformed_sum = 0.0;
            for (int c = 0; c < num_clauses; c++)
            {
                if (org_clause_weight[c] != top_clause_weight)
                    transformed_sum += pow(
                        static_cast<double>(org_clause_weight[c]) / mean_soft_weight,
                        quality_weight_power);
            }
            const double objective_normalizer =
                static_cast<double>(num_sclauses) / transformed_sum;
            coe_tuned_weight = quality_weight_scale * objective_normalizer;
            bool structure_applicable = quality_structure_mix != 0.0;
            int maximum_hard_clause_length = 0;
            if (structure_applicable)
            {
                for (int c = 0; c < num_clauses; ++c)
                {
                    if (org_clause_weight[c] != top_clause_weight)
                        continue;
                    if (clause_lit_count[c] > maximum_hard_clause_length)
                        maximum_hard_clause_length = clause_lit_count[c];
                    if (maximum_hard_clause_length > 8)
                    {
                        structure_applicable = false;
                        break;
                    }
                }
                cout << "c quality_structure_weight applicable "
                     << (structure_applicable ? 1 : 0)
                     << " max_hard_clause_length "
                     << maximum_hard_clause_length
                     << " bounded_width_limit 8" << endl;
            }
            if (!structure_applicable)
            {
                for (int c = 0; c < num_clauses; c++)
                {
                    if (org_clause_weight[c] != top_clause_weight)
                    {
                        const double objective_component = objective_normalizer * pow(
                            static_cast<double>(org_clause_weight[c]) / mean_soft_weight,
                            quality_weight_power);
                        tuned_org_clause_weight[c] = quality_weight_scale *
                            ((1.0 - quality_weight_mix) +
                             quality_weight_mix * objective_component);
                    }
                }
            }
            else
            {
                vector<double> hard_positive(static_cast<size_t>(num_vars) + 1, 0.0);
                vector<double> hard_negative(static_cast<size_t>(num_vars) + 1, 0.0);
                for (int v = 1; v <= num_vars; ++v)
                {
                    for (int i = 0; i < var_lit_count[v]; ++i)
                    {
                        const lit &occurrence = var_lit[v][i];
                        if (org_clause_weight[occurrence.clause_num] != top_clause_weight)
                            continue;
                        double contribution = 1.0;
                        if (quality_structure_mode >= 2)
                        {
                            const int alternatives =
                                clause_lit_count[occurrence.clause_num] - 1;
                            contribution = 1.0 / (alternatives > 0 ? alternatives : 1);
                        }
                        if (occurrence.sense)
                            hard_positive[v] += contribution;
                        else
                            hard_negative[v] += contribution;
                    }
                }

                vector<double> clause_rigidity(
                    static_cast<size_t>(num_clauses), 1.0);
                double rigidity_sum = 0.0;
                for (int c = 0; c < num_clauses; ++c)
                {
                    if (org_clause_weight[c] == top_clause_weight)
                        continue;
                    double pressure_sum = 0.0;
                    double minimum_pressure = 0.0;
                    int literal_count = 0;
                    for (lit *p = clause_lit[c]; p->var_num != 0; ++p)
                    {
                        const double same = p->sense
                            ? hard_positive[p->var_num] : hard_negative[p->var_num];
                        const double opposite = p->sense
                            ? hard_negative[p->var_num] : hard_positive[p->var_num];
                        const double literal_pressure =
                            log(1.0 + opposite + 0.25 * same);
                        pressure_sum += literal_pressure;
                        if (literal_count == 0 || literal_pressure < minimum_pressure)
                            minimum_pressure = literal_pressure;
                        ++literal_count;
                    }
                    const double aggregate_pressure = literal_count == 0
                        ? 0.0
                        : (quality_structure_mode == 3
                            ? minimum_pressure
                            : pressure_sum / literal_count);
                    clause_rigidity[c] = 1.0 + aggregate_pressure;
                    rigidity_sum += clause_rigidity[c];
                }
                const double mean_rigidity = rigidity_sum / num_sclauses;
                double combined_sum = 0.0;
                for (int c = 0; c < num_clauses; ++c)
                {
                    if (org_clause_weight[c] == top_clause_weight)
                        continue;
                    const double objective_component = objective_normalizer * pow(
                        static_cast<double>(org_clause_weight[c]) / mean_soft_weight,
                        quality_weight_power);
                    const double objective_fusion = (1.0 - quality_weight_mix) +
                        quality_weight_mix * objective_component;
                    const double structure_factor = pow(
                        clause_rigidity[c] / mean_rigidity,
                        quality_structure_mix);
                    tuned_org_clause_weight[c] = objective_fusion * structure_factor;
                    combined_sum += tuned_org_clause_weight[c];
                }
                const double combined_normalizer = quality_weight_scale *
                    static_cast<double>(num_sclauses) / combined_sum;
                for (int c = 0; c < num_clauses; ++c)
                    if (org_clause_weight[c] != top_clause_weight)
                        tuned_org_clause_weight[c] *= combined_normalizer;
            }
#else
            coe_tuned_weight = (double)(coe_soft_clause_weight * num_sclauses) / double(top_clause_weight - 1);
            for (int c = 0; c < num_clauses; c++)
            {
                if (org_clause_weight[c] != top_clause_weight)
                {
                    tuned_org_clause_weight[c] = (double)org_clause_weight[c] * coe_tuned_weight;
                }
            }
#endif
        }
        else
        {
            softclause_weight_threshold = 0;
            soft_smooth_probability = 1E-3;
            hd_count_threshold = 22;
            rdprob = 0.036;
            rwprob = 0.48;
            s_inc = 1.0;
            for (int c = 0; c < num_clauses; c++)
            {
                tuned_org_clause_weight[c] = org_clause_weight[c];
            }
        }
    }
    else
    {
        if (0 == num_hclauses)
        {
            hd_count_threshold = 94;
            coe_soft_clause_weight = 397;
            rdprob = 0.007;
            rwprob = 0.047;
            soft_smooth_probability = 0.002;
            softclause_weight_threshold = 550;
        }
    }
    Decimation deci(var_lit, var_lit_count, clause_lit, org_clause_weight, top_clause_weight);
    deci.make_space(num_clauses, num_vars);

    opt_unsat_weight = __LONG_LONG_MAX__;
    for (tries = 1; ; ++tries)
    {
#ifdef NUWLS_QUALITY_SEARCH
        if (tries == 1 && !quality_seed_solution.empty())
        {
            init(quality_seed_solution);
        }
        else
        {
            deci.init(local_opt_soln, best_soln, unit_clause, unit_clause_count, clause_lit_count);
            deci.unit_prosess();
            init(deci.fix);
        }
#else
        deci.init(local_opt_soln, best_soln, unit_clause, unit_clause_count, clause_lit_count);
        deci.unit_prosess();
        init(deci.fix);
#endif

        long long local_opt = __LONG_LONG_MAX__;
        max_flips = max_non_improve_flip;
        for (step = 1; step < max_flips; ++step)
        {
            if (step % 1000 == 0 && get_runtime() >= cutoff_time)
                return;
            if (hard_unsat_nb == 0)
            {
                local_soln_feasible = 1;
                if (local_opt > soft_unsat_weight)
                {
                    local_opt = soft_unsat_weight;
                    max_flips = step + max_non_improve_flip;
                }
                if (soft_unsat_weight < opt_unsat_weight)
                {
                    opt_time = get_runtime();
                    //cout << "o " << soft_unsat_weight << " " << total_step << " " << tries << " " << soft_smooth_probability << " " << opt_time << endl;
                    cout << "o " << soft_unsat_weight << " " << opt_time << endl;
                    opt_unsat_weight = soft_unsat_weight;

                    for (int v = 1; v <= num_vars; ++v)
                        best_soln[v] = cur_soln[v];
                }
                if (best_soln_feasible == 0)
                {
                    best_soln_feasible = 1;
#ifdef NUWLS_QUALITY_SEARCH
                    if (quality_phase_enabled)
                    {
                        quality_phase_active = true;
                        apply_quality_weights();
                        cout << "c quality_phase activated 1 weight_power "
                             << quality_weight_power << " weight_scale "
                             << quality_weight_scale << " weight_mix "
                             << quality_weight_mix << " weight_anchor "
                             << quality_weight_anchor << " structure_mix "
                             << quality_structure_mix << " structure_mode "
                             << quality_structure_mode << " hard_guard "
                             << quality_hard_guard_factor << " time "
                             << get_runtime() << endl;
                    }
#else
                    // break;
#endif
                }
            }
            
            int flipvar = pick_var();
            flip(flipvar);
            time_stamp[flipvar] = step;
            total_step++;
        }
    }
}

void ISDist::hard_increase_weights()
{
    int i, c, v;
    for (i = 0; i < hardunsat_stack_fill_pointer; ++i)
    {
        c = hardunsat_stack[i];
        clause_weight[c] += h_inc;

        if (clause_weight[c] == (h_inc + 1))
            large_weight_clauses[large_weight_clauses_count++] = c;

        for (lit *p = clause_lit[c]; (v = p->var_num) != 0; p++)
        {
            score[v] += h_inc;
            if (score[v] > 0 && already_in_goodvar_stack[v] == -1)
            {
                already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
                mypush(v, goodvar_stack);
            }
        }
    }
    return;
}

void ISDist::soft_increase_weights()
{
    int i, c, v;

    if (1 == problem_weighted)
    {
        for (i = 0; i < softunsat_stack_fill_pointer; ++i)
        {
            c = softunsat_stack[i];
            if (clause_weight[c] >= tuned_org_clause_weight[c] + softclause_weight_threshold)
                continue;
            else
                clause_weight[c] += s_inc;

            if (clause_weight[c] > s_inc && already_in_soft_large_weight_stack[c] == 0)
            {
                already_in_soft_large_weight_stack[c] = 1;
                soft_large_weight_clauses[soft_large_weight_clauses_count++] = c;
            }
            for (lit *p = clause_lit[c]; (v = p->var_num) != 0; p++)
            {
                score[v] += s_inc;
                if (score[v] > 0 && already_in_goodvar_stack[v] == -1)
                {
                    already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
                    mypush(v, goodvar_stack);
                }
            }
        }
    }
    else
    {
        for (i = 0; i < softunsat_stack_fill_pointer; ++i)
        {
            c = softunsat_stack[i];
            if (clause_weight[c] >= coe_soft_clause_weight + softclause_weight_threshold)
                continue;
            else
                clause_weight[c] += s_inc;

            if (clause_weight[c] > s_inc && already_in_soft_large_weight_stack[c] == 0)
            {
                already_in_soft_large_weight_stack[c] = 1;
                soft_large_weight_clauses[soft_large_weight_clauses_count++] = c;
            }
            for (lit *p = clause_lit[c]; (v = p->var_num) != 0; p++)
            {
                score[v] += s_inc;
                if (score[v] > 0 && already_in_goodvar_stack[v] == -1)
                {
                    already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
                    mypush(v, goodvar_stack);
                }
            }
        }
    }
    return;
}

void ISDist::hard_smooth_weights()
{
    int i, clause, v;
    for (i = 0; i < large_weight_clauses_count; i++)
    {
        clause = large_weight_clauses[i];
        if (sat_count[clause] > 0)
        {
            clause_weight[clause] -= h_inc;

            if (clause_weight[clause] == 1)
            {
                large_weight_clauses[i] = large_weight_clauses[--large_weight_clauses_count];
                i--;
            }
            if (sat_count[clause] == 1)
            {
                v = sat_var[clause];
                score[v] += h_inc;
                if (score[v] > 0 && already_in_goodvar_stack[v] == -1)
                {
                    already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
                    mypush(v, goodvar_stack);
                }
            }
        }
    }
    return;
}

void ISDist::soft_smooth_weights()
{
    int i, clause, v;

    for (i = 0; i < soft_large_weight_clauses_count; i++)
    {
        clause = soft_large_weight_clauses[i];
        if (sat_count[clause] > 0)
        {
            double decrement = s_inc;
#ifdef NUWLS_QUALITY_SEARCH
            if (quality_phase_active && problem_weighted && quality_weight_anchor > 0.0)
            {
                const double anchor_floor =
                    quality_weight_anchor * tuned_org_clause_weight[clause];
                if (clause_weight[clause] - decrement < anchor_floor)
                    decrement = clause_weight[clause] - anchor_floor;
                if (decrement <= 0.0)
                    continue;
            }
#endif
            clause_weight[clause] -= decrement;
            if (clause_weight[clause] <= s_inc && already_in_soft_large_weight_stack[clause] == 1)
            {
                already_in_soft_large_weight_stack[clause] = 0;
                soft_large_weight_clauses[i] = soft_large_weight_clauses[--soft_large_weight_clauses_count];
                i--;
            }
            if (sat_count[clause] == 1)
            {
                v = sat_var[clause];
                score[v] += decrement;
                if (score[v] > 0 && already_in_goodvar_stack[v] == -1)
                {
                    already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
                    mypush(v, goodvar_stack);
                }
            }
        }
    }
    return;
}

void ISDist::update_clause_weights()
{
    if (num_hclauses > 0)
    {
        // update hard clause weight
        if (1 == local_soln_feasible && ((rand() % MY_RAND_MAX_INT) * BASIC_SCALE) < smooth_probability && large_weight_clauses_count > large_clause_count_threshold)
        {
            hard_smooth_weights();
        }
        else
        {
            hard_increase_weights();
        }

        // update soft clause weight
        // if (1 == local_soln_feasible && ((rand() % MY_RAND_MAX_INT) * BASIC_SCALE) < soft_smooth_probability && soft_large_weight_clauses_count > soft_large_clause_count_threshold)
        if (soft_unsat_weight >= opt_unsat_weight)
        {
            if (((rand() % MY_RAND_MAX_INT) * BASIC_SCALE) < soft_smooth_probability && soft_large_weight_clauses_count > soft_large_clause_count_threshold)
            {
                soft_smooth_weights();
            }
            else if (0 == hard_unsat_nb)
            {
                soft_increase_weights();
            }
        }
    }
    else
    {
        if (((rand() % MY_RAND_MAX_INT) * BASIC_SCALE) < soft_smooth_probability && soft_large_weight_clauses_count > soft_large_clause_count_threshold)
        {
            soft_smooth_weights();
        }
        else
        {
            soft_increase_weights();
        }
    }
}

#endif
