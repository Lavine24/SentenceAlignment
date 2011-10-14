#include "alignment_models/model1.h"

#include <limits>
#include <tr1/unordered_set>

#include "util/math_util.h"

using std::tr1::unordered_set;

void Model1::InitDataStructures(const ParallelCorpus& pc) {
  source_vocab_size_ = pc.source_vocab().size();
  target_vocab_size_ = pc.target_vocab().size();
  // For each source word, keep track of how many possible target words it can
  // generate. The null word can generate anything.
  vector<unordered_set<int> > targets_per_source;
  targets_per_source.resize(source_vocab_size_);
  for (int i = 0; i < pc.size(); ++i) {
    // Put all of the source and target words in the document pair into sets,
    // and 
    const DocumentPair& doc_pair = pc.GetDocPair(i);
    unordered_set<int> source_words, target_words;
    for (int s = 0; s < doc_pair.first.size(); ++s) {
      for (int w = 0; w < doc_pair.first.at(s).size(); ++w) {
        source_words.insert(doc_pair.first.at(s).at(w));
      }
    }
    for (int t = 0; t < doc_pair.second.size(); ++t) {
      for (int w = 0; w < doc_pair.second.at(t).size(); ++w) {
        target_words.insert(doc_pair.second.at(t).at(w));
      }
    }
    unordered_set<int>::const_iterator it_s, it_t;
    for (it_s = source_words.begin(); it_s != source_words.end(); ++it_s) {
      for (it_t = target_words.begin(); it_t != target_words.end(); ++it_t) {
        targets_per_source[*it_s].insert(*it_t);
      }
    }
  }
  // Initialize the parameters uniformly.
  // Handle the null word seperately, since it can generate all target words.
  for (int t = 1; t < target_vocab_size_; ++t) {
    lookup(&t_table_, 0, t) = log(1.0 / (target_vocab_size_ - 1));
  }
  for (int s = 1; s < source_vocab_size_; ++s) {
    double uniform_prob = log(1.0 / targets_per_source[s].size());
    unordered_set<int>::const_iterator it_t;
    for (it_t = targets_per_source[s].begin();
         it_t != targets_per_source[s].end(); ++it_t) {
      lookup(&t_table_, s, *it_t) = uniform_prob;
    }
  }
}

double Model1::ScorePair(const Sentence& source, const Sentence& target) const {
  double result = 0.0;
  // The uniform alignment probability.
  double alignment_prob = log(1.0 / target.size());
  for (int t = 0; t < target.size(); ++t) {
    // The probability of generating this target word, initialized with the
    // probability of it being generated by the null source word.
    double t_prob = lookup(&t_table_, 0, target[t]);
    for (int s = 0; s < source.size(); ++s) {
      t_prob = MathUtil::LogAdd(
          t_prob, lookup(&t_table_, source[s], target[t]));
    }
    // Add the probability of generating this word to the 
    // The alignment probability can be factored out.
    result += t_prob + alignment_prob;
  }
  return result;
}

void Model1::ClearExpectedCounts() {
  // (s) counts
  for (int s = 0; s < source_vocab_size_; ++s) {
    lookup(&expected_counts_, s, 0) = -std::numeric_limits<double>::max();
  }
  // (t,s) counts
  TTable::const_iterator it;
  for (it = t_table_.begin(); it != t_table_.end(); ++it) {
    expected_counts_[it->first] = -std::numeric_limits<double>::max();
  }
}

double Model1::EStep(
    const Sentence& source, const Sentence& target, double weight) {
  // Update c(s)
  lookup(&expected_counts_, 0, 0) = MathUtil::LogAdd(
      lookup(&expected_counts_, 0, 0), weight);
  for (int s = 0; s < source.size(); ++s) {
    lookup(&expected_counts_, source[s], 0) = MathUtil::LogAdd(
        lookup(&expected_counts_, source[s], 0), weight);
  }
  // The uniform alignment probability.
  double result = 0.0;
  double alignment_prob = log(1.0 / target.size());
  for (int t = 0; t < target.size(); ++t) {
    // The probability of generating this target word, initialized with the
    // probability of it being generated by the null source word.
    double t_prob = lookup(&t_table_, 0, target[t]);
    for (int s = 0; s < source.size(); ++s) {
      t_prob = MathUtil::LogAdd(
          t_prob, lookup(&t_table_, source[s], target[t]));
    }
    // Add the probability of generating this word to the 
    // The alignment probability can be factored out.
    result += t_prob + alignment_prob;
    // Update the expected counts using t_prob as the denominator
    // Null word generation
    lookup(&expected_counts_, 0, target[t]) = MathUtil::LogAdd(
        lookup(&expected_counts_, 0, target[t]),
        lookup(&t_table_, 0, target[t]) - t_prob + weight);
    for (int s = 0; s < source.size(); ++s) {
      lookup(&expected_counts_, source[s], target[t]) = MathUtil::LogAdd(
          lookup(&expected_counts_, source[s], target[t]),
          lookup(&t_table_, source[s], target[t]) - t_prob + weight);
    }
  }
  return result;
}

void Model1::MStep() {
  TTable::iterator it;
  for (it = t_table_.begin(); it != t_table_.end(); ++it) {
    // c(t,s) / c(s) in the log domain
    it->second = expected_counts_[it->first]
        - lookup(&expected_counts_, it->first.first, 0);
  }
}
