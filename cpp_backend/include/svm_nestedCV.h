#include <bits/stdc++.h>
#include "svm.h"
using namespace std;

// =========================
// Config
// =========================
struct NestedCVConfig {
    int outer_k = 5;
    int inner_k = 3;
    int outer_repeats = 5;               // 小資料建議 >1
    bool use_balanced_class_weight = true;
    bool use_rbf = true;                 // true: RBF, false: Linear
    bool verbose = true;
    uint32_t seed = 42;

    // RBF grid (較保守，避免小資料過度搜尋)
    vector<double> C_grid;
    vector<double> gamma_grid;

    NestedCVConfig() {
        // 小資料可先用較小 grid；必要時再擴大
        for (int e : {-3, -1, 1, 3, 5, 7}) C_grid.push_back(pow(2.0, e));
        for (int e : {-9, -7, -5, -3, -1, 1}) gamma_grid.push_back(pow(2.0, e));
    }
};

// =========================
// Metrics
// =========================
struct Metrics {
    long long tp = 0, tn = 0, fp = 0, fn = 0;

    long long n() const { return tp + tn + fp + fn; }

    double accuracy() const {
        auto d = n();
        return d ? double(tp + tn) / d : 0.0;
    }
    double precision_pos() const {
        long long d = tp + fp;
        return d ? double(tp) / d : 0.0;
    }
    double recall_pos() const { // sensitivity / TPR
        long long d = tp + fn;
        return d ? double(tp) / d : 0.0;
    }
    double specificity() const { // TNR
        long long d = tn + fp;
        return d ? double(tn) / d : 0.0;
    }
    double f1_pos() const {
        double p = precision_pos(), r = recall_pos();
        return (p + r > 0) ? 2.0 * p * r / (p + r) : 0.0;
    }
    double balanced_accuracy() const {
        return 0.5 * (recall_pos() + specificity());
    }
    double mcc() const {
        double a = double(tp), b = double(fp), c = double(fn), d = double(tn);
        double denom = sqrt((a+b)*(a+c)*(d+b)*(d+c));
        if (denom <= 0) return 0.0;
        return (a*d - b*c) / denom;
    }

    void add_one(int y_true, int y_pred) {
        bool pos_t = (y_true > 0);
        bool pos_p = (y_pred > 0);
        if (pos_t && pos_p) tp++;
        else if (!pos_t && !pos_p) tn++;
        else if (!pos_t && pos_p) fp++;
        else fn++;
    }

    void merge(const Metrics& m) {
        tp += m.tp; tn += m.tn; fp += m.fp; fn += m.fn;
    }
};

struct FoldSummary {
    int repeat_id = 0;
    int outer_fold = 0;
    double best_C = 0;
    double best_gamma = 0;
    double inner_score = 0;
    Metrics outer_metrics;
};

struct NestedCVResult {
    Metrics aggregated;
    vector<FoldSummary> folds;
};

// =========================
// Utility
// =========================
template <class T>
vector<T> gather_vec(const vector<T>& v, const vector<int>& idx) {
    vector<T> out;
    out.reserve(idx.size());
    for (int i : idx) out.push_back(v[i]);
    return out;
}

vector<vector<double>> gather_X(const vector<vector<double>>& X, const vector<int>& idx) {
    vector<vector<double>> out;
    out.reserve(idx.size());
    for (int i : idx) out.push_back(X[i]);
    return out;
}

struct Standardizer {
    vector<double> mean, stdev;

    void fit(const vector<vector<double>>& X) {
        int n = (int)X.size();
        int d = n ? (int)X[0].size() : 0;
        mean.assign(d, 0.0);
        stdev.assign(d, 0.0);
        if (n == 0 || d == 0) return;

        for (auto& row : X)
            for (int j = 0; j < d; ++j) mean[j] += row[j];
        for (int j = 0; j < d; ++j) mean[j] /= n;

        for (auto& row : X)
            for (int j = 0; j < d; ++j) {
                double diff = row[j] - mean[j];
                stdev[j] += diff * diff;
            }

        for (int j = 0; j < d; ++j) {
            stdev[j] = sqrt(stdev[j] / n);   // population std
            if (stdev[j] < 1e-12) stdev[j] = 1.0;
        }
    }

    vector<vector<double>> transform(const vector<vector<double>>& X) const {
        vector<vector<double>> out = X;
        for (auto& row : out) {
            for (int j = 0; j < (int)row.size(); ++j) {
                row[j] = (row[j] - mean[j]) / stdev[j];
            }
        }
        return out;
    }
};

vector<vector<int>> stratified_kfold_indices(const vector<int>& y, int k, uint32_t seed) {
    vector<int> pos, neg;
    for (int i = 0; i < (int)y.size(); ++i) {
        if (y[i] > 0) pos.push_back(i);
        else neg.push_back(i);
    }

    mt19937 rng(seed);
    shuffle(pos.begin(), pos.end(), rng);
    shuffle(neg.begin(), neg.end(), rng);

    // k 不要超過任何一類的樣本數，避免某些 fold 某類完全沒有
    int pos_n = (int)pos.size();
    int neg_n = (int)neg.size();
    int max_safe_k = max(2, min(pos_n, neg_n)); // 至少2-fold，但小心極端情況
    if (pos_n == 0 || neg_n == 0) {
        throw runtime_error("資料只有單一類別，無法做二元分類 SVM。");
    }
    k = min(k, max_safe_k);

    vector<vector<int>> folds(k);
    for (int i = 0; i < (int)pos.size(); ++i) folds[i % k].push_back(pos[i]);
    for (int i = 0; i < (int)neg.size(); ++i) folds[i % k].push_back(neg[i]);
    for (auto& f : folds) shuffle(f.begin(), f.end(), rng);

    return folds;
}

vector<int> make_train_idx_from_folds(const vector<vector<int>>& folds, int held_out_fold) {
    vector<int> idx;
    for (int i = 0; i < (int)folds.size(); ++i) {
        if (i == held_out_fold) continue;
        idx.insert(idx.end(), folds[i].begin(), folds[i].end());
    }
    return idx;
}

// =========================
// LIBSVM wrappers
// =========================
struct LibSVMProblem {
    svm_problem prob{};
    vector<double> y_storage;
    vector<vector<svm_node>> x_nodes;
    vector<svm_node*> x_ptrs;

    LibSVMProblem(const vector<vector<double>>& X, const vector<int>& y) {
        int n = (int)X.size();
        int d = n ? (int)X[0].size() : 0;

        y_storage.resize(n);
        x_nodes.resize(n);
        x_ptrs.resize(n);

        for (int i = 0; i < n; ++i) {
            y_storage[i] = (double)y[i];
            x_nodes[i].resize(d + 1);
            for (int j = 0; j < d; ++j) {
                x_nodes[i][j].index = j + 1;
                x_nodes[i][j].value = X[i][j];
            }
            x_nodes[i][d].index = -1;
            x_nodes[i][d].value = 0.0;
            x_ptrs[i] = x_nodes[i].data();
        }

        prob.l = n;
        prob.y = y_storage.data();
        prob.x = x_ptrs.data();
    }
};

svm_parameter base_params(int num_features, bool use_rbf) {
    svm_parameter p{};
    p.svm_type = C_SVC;
    p.kernel_type = use_rbf ? RBF : LINEAR;
    p.degree = 3;
    p.gamma = (num_features > 0) ? 1.0 / num_features : 0.5; // for RBF
    p.coef0 = 0;
    p.nu = 0.5;
    p.cache_size = 200;
    p.C = 1.0;
    p.eps = 1e-3;
    p.p = 0.1;
    p.shrinking = 1;
    p.probability = 0;
    p.nr_weight = 0;
    p.weight_label = nullptr;
    p.weight = nullptr;
    return p;
}

struct ClassWeightStorage {
    vector<int> labels;       // e.g. {+1, -1}
    vector<double> weights;   // matching labels order
};

ClassWeightStorage make_balanced_class_weights(const vector<int>& y_train) {
    int n_pos = 0, n_neg = 0;
    for (int y : y_train) (y > 0) ? n_pos++ : n_neg++;

    ClassWeightStorage cw;
    cw.labels = {+1, -1};
    cw.weights = {1.0, 1.0};

    int n = (int)y_train.size();
    // sklearn-like balanced: n_samples / (n_classes * count_class)
    if (n_pos > 0) cw.weights[0] = double(n) / (2.0 * n_pos);
    if (n_neg > 0) cw.weights[1] = double(n) / (2.0 * n_neg);
    return cw;
}

int svm_predict_label(const svm_model* model, const vector<double>& x) {
    vector<svm_node> nodes(x.size() + 1);
    for (int j = 0; j < (int)x.size(); ++j) {
        nodes[j].index = j + 1;
        nodes[j].value = x[j];
    }
    nodes[x.size()].index = -1;
    nodes[x.size()].value = 0.0;
    double pred = svm_predict(model, nodes.data());
    return (int)pred;
}

Metrics evaluate_model(const svm_model* model,
                       const vector<vector<double>>& X,
                       const vector<int>& y) {
    Metrics m;
    for (int i = 0; i < (int)X.size(); ++i) {
        int pred = svm_predict_label(model, X[i]);
        m.add_one(y[i], pred);
    }
    return m;
}

// 給 libsvm 靜音（避免訓練時狂印）
void libsvm_quiet_print(const char*) {}
void setup_libsvm_quiet_mode() {
    svm_set_print_string_function(libsvm_quiet_print);
}

// =========================
// Inner score: balanced accuracy (for model selection)
// =========================
double score_balanced_accuracy_on_fold(const vector<vector<double>>& X_train,
                                       const vector<int>& y_train,
                                       const vector<vector<int>>& inner_folds,
                                       int held_out_inner_fold,
                                       const NestedCVConfig& cfg,
                                       double C, double gamma) {
    vector<int> tr_idx = make_train_idx_from_folds(inner_folds, held_out_inner_fold);
    vector<int> va_idx = inner_folds[held_out_inner_fold];

    auto Xi_tr_raw = gather_X(X_train, tr_idx);
    auto yi_tr = gather_vec(y_train, tr_idx);
    auto Xi_va_raw = gather_X(X_train, va_idx);
    auto yi_va = gather_vec(y_train, va_idx);

    // 再次注意：scaler 只能 fit inner-train，不能看 inner-val
    Standardizer scaler;
    scaler.fit(Xi_tr_raw);
    auto Xi_tr = scaler.transform(Xi_tr_raw);
    auto Xi_va = scaler.transform(Xi_va_raw);

    LibSVMProblem prob_tr(Xi_tr, yi_tr);
    int d = Xi_tr.empty() ? 0 : (int)Xi_tr[0].size();

    svm_parameter param = base_params(d, cfg.use_rbf);
    param.C = C;
    if (cfg.use_rbf) param.gamma = gamma;

    ClassWeightStorage cw;
    if (cfg.use_balanced_class_weight) {
        cw = make_balanced_class_weights(yi_tr);
        param.nr_weight = (int)cw.labels.size();
        param.weight_label = cw.labels.data();
        param.weight = cw.weights.data();
    }

    const char* err = svm_check_parameter(&prob_tr.prob, &param);
    if (err) return -1.0;

    svm_model* model = svm_train(&prob_tr.prob, &param);
    Metrics m = evaluate_model(model, Xi_va, yi_va);
    svm_free_and_destroy_model(&model);

    return m.balanced_accuracy();
}

struct BestParam {
    double C = 1.0;
    double gamma = 0.0; // linear 時可忽略
    double inner_score = -1.0;
};

BestParam inner_grid_search_balanced_acc(const vector<vector<double>>& X_train_scaled,
                                         const vector<int>& y_train,
                                         const NestedCVConfig& cfg,
                                         uint32_t seed_for_inner) {
    BestParam best;
    auto inner_folds = stratified_kfold_indices(y_train, cfg.inner_k, seed_for_inner);

    vector<double> gamma_grid = cfg.use_rbf ? cfg.gamma_grid : vector<double>{0.0};

    for (double C : cfg.C_grid) {
        for (double gamma : gamma_grid) {
            double sum_score = 0.0;
            bool valid = true;

            for (int inf = 0; inf < (int)inner_folds.size(); ++inf) {
                double s = score_balanced_accuracy_on_fold(
                    X_train_scaled, y_train, inner_folds, inf, cfg, C, gamma
                );
                if (s < 0) { valid = false; break; }
                sum_score += s;
            }

            if (!valid) continue;
            double mean_score = sum_score / inner_folds.size();

            // tie-breaker: 偏好較小 C（較保守）→ 小資料常比較穩
            if (mean_score > best.inner_score + 1e-12 ||
                (fabs(mean_score - best.inner_score) <= 1e-12 && C < best.C)) {
                best.C = C;
                best.gamma = gamma;
                best.inner_score = mean_score;
            }
        }
    }

    return best;
}

// =========================
// Main nested CV (small-data / positive-majority friendly)
// =========================
NestedCVResult nested_cv_report_small_imbalanced(const vector<vector<double>>& X_all,
                                                 const vector<int>& y_all,
                                                 const NestedCVConfig& cfg) {
    setup_libsvm_quiet_mode();

    if (X_all.empty()) throw runtime_error("X is empty.");
    if (X_all.size() != y_all.size()) throw runtime_error("X/Y size mismatch.");
    int d = (int)X_all[0].size();
    for (auto& row : X_all) {
        if ((int)row.size() != d) throw runtime_error("X has inconsistent feature dimension.");
    }

    int n_pos = 0, n_neg = 0;
    for (int y : y_all) (y > 0) ? n_pos++ : n_neg++;
    if (n_pos == 0 || n_neg == 0) throw runtime_error("Need both positive and negative samples.");

    NestedCVResult result;

    if (cfg.verbose) {
        cout << "Total samples = " << X_all.size()
             << "  (+)=" << n_pos << "  (-)=" << n_neg
             << "  pos_ratio=" << fixed << setprecision(4)
             << (double)n_pos / X_all.size() << "\n";
        cout << "Outer " << cfg.outer_k << "-fold x " << cfg.outer_repeats
             << " repeats, Inner " << cfg.inner_k << "-fold, "
             << (cfg.use_rbf ? "RBF" : "Linear")
             << ", class_weight=" << (cfg.use_balanced_class_weight ? "balanced" : "none")
             << "\n";
    }

    for (int rep = 0; rep < cfg.outer_repeats; ++rep) {
        uint32_t outer_seed = cfg.seed + 1009u * rep;
        auto outer_folds = stratified_kfold_indices(y_all, cfg.outer_k, outer_seed);

        for (int of = 0; of < (int)outer_folds.size(); ++of) {
            vector<int> test_idx = outer_folds[of];
            vector<int> train_idx = make_train_idx_from_folds(outer_folds, of);

            auto X_train_raw = gather_X(X_all, train_idx);
            auto y_train = gather_vec(y_all, train_idx);
            auto X_test_raw = gather_X(X_all, test_idx);
            auto y_test = gather_vec(y_all, test_idx);

            // Outer scaler: fit on outer-train only
            Standardizer outer_scaler;
            outer_scaler.fit(X_train_raw);
            auto X_train_scaled = outer_scaler.transform(X_train_raw);
            auto X_test_scaled = outer_scaler.transform(X_test_raw);

            // Inner search on outer-train
            BestParam best = inner_grid_search_balanced_acc(
                X_train_scaled, y_train, cfg, cfg.seed + 100000u + rep * 97u + of
            );

            // Train final model on full outer-train with best params
            LibSVMProblem prob_outer(X_train_scaled, y_train);
            svm_parameter param = base_params(d, cfg.use_rbf);
            param.C = best.C;
            if (cfg.use_rbf) param.gamma = best.gamma;

            ClassWeightStorage cw;
            if (cfg.use_balanced_class_weight) {
                cw = make_balanced_class_weights(y_train);
                param.nr_weight = (int)cw.labels.size();
                param.weight_label = cw.labels.data();
                param.weight = cw.weights.data();
            }

            const char* err = svm_check_parameter(&prob_outer.prob, &param);
            if (err) {
                cerr << "[WARN] outer fold skipped due to invalid params: " << err << "\n";
                continue;
            }

            svm_model* model = svm_train(&prob_outer.prob, &param);
            Metrics m_outer = evaluate_model(model, X_test_scaled, y_test);
            svm_free_and_destroy_model(&model);

            result.aggregated.merge(m_outer);

            FoldSummary fs;
            fs.repeat_id = rep + 1;
            fs.outer_fold = of + 1;
            fs.best_C = best.C;
            fs.best_gamma = best.gamma;
            fs.inner_score = best.inner_score;
            fs.outer_metrics = m_outer;
            result.folds.push_back(fs);

            if (cfg.verbose) {
                int ytest_pos = count_if(y_test.begin(), y_test.end(), [](int v){ return v > 0; });
                int ytest_neg = (int)y_test.size() - ytest_pos;
                int pred_pos = (int)(m_outer.tp + m_outer.fp);
                int pred_neg = (int)(m_outer.tn + m_outer.fn);

                cout << "[Rep " << (rep + 1) << " Fold " << (of + 1) << "/" << outer_folds.size() << "] "
                     << "best(C=" << best.C;
                if (cfg.use_rbf) cout << ", g=" << best.gamma;
                cout << "), inner_bal_acc=" << fixed << setprecision(4) << best.inner_score
                     << " | test truth +/ - = " << ytest_pos << "/" << ytest_neg
                     << " pred +/ - = " << pred_pos << "/" << pred_neg
                     << " | bal_acc=" << m_outer.balanced_accuracy()
                     << " f1=" << m_outer.f1_pos()
                     << "\n";
            }
        }
    }

    return result;
}

// =========================
// Report printing
// =========================
void print_final_report(const NestedCVResult& r) {
    const Metrics& m = r.aggregated;
    cout << "\n=== Nested CV Report (outer test aggregated, repeated) ===\n";
    cout << "Confusion Matrix:\n";
    cout << "TP=" << m.tp << "  FP=" << m.fp << "\n";
    cout << "FN=" << m.fn << "  TN=" << m.tn << "\n";

    cout << fixed << setprecision(4);
    cout << "Accuracy          : " << m.accuracy() << "\n";
    cout << "Precision (pos)   : " << m.precision_pos() << "\n";
    cout << "Recall / TPR (pos): " << m.recall_pos() << "\n";
    cout << "Specificity / TNR : " << m.specificity() << "\n";
    cout << "F1 (pos)          : " << m.f1_pos() << "\n";
    cout << "Balanced Accuracy : " << m.balanced_accuracy() << "\n";
    cout << "MCC               : " << m.mcc() << "\n";

    if (!r.folds.empty()) {
        vector<double> bas, f1s, inners;
        bas.reserve(r.folds.size());
        f1s.reserve(r.folds.size());
        inners.reserve(r.folds.size());

        for (auto& f : r.folds) {
            bas.push_back(f.outer_metrics.balanced_accuracy());
            f1s.push_back(f.outer_metrics.f1_pos());
            inners.push_back(f.inner_score);
        }

        auto mean_std = [](const vector<double>& v) -> pair<double,double> {
            if (v.empty()) return {0.0, 0.0};
            double mu = accumulate(v.begin(), v.end(), 0.0) / v.size();
            double var = 0.0;
            for (double x : v) var += (x - mu) * (x - mu);
            var /= v.size();
            return {mu, sqrt(var)};
        };

        auto [ba_mu, ba_sd] = mean_std(bas);
        auto [f1_mu, f1_sd] = mean_std(f1s);
        auto [in_mu, in_sd] = mean_std(inners);

        cout << "\nPer-fold summary (" << r.folds.size() << " outer evaluations):\n";
        cout << "Inner balanced acc mean±sd : " << in_mu << " ± " << in_sd << "\n";
        cout << "Outer balanced acc mean±sd : " << ba_mu << " ± " << ba_sd << "\n";
        cout << "Outer F1(pos) mean±sd      : " << f1_mu << " ± " << f1_sd << "\n";
    }
}