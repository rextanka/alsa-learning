/**
 * @file AudioGraph.hpp
 * @brief Manages a collection of connected audio processors.
 */

#ifndef AUDIO_AUDIO_GRAPH_HPP
#define AUDIO_AUDIO_GRAPH_HPP

#include "Processor.hpp"
#include "BufferPool.hpp"
#include <vector>
#include <memory>
#include <algorithm>

namespace audio {

/**
 * @brief Manages a processing chain of audio nodes.
 * 
 * In this implementation, we use a simple linear execution chain 
 * which is sufficient for Source -> Modifier -> Output topologies.
 */
class AudioGraph : public Processor {
public:
    explicit AudioGraph(size_t buffer_size = 512)
        : buffer_pool_(std::make_shared<BufferPool>(buffer_size))
    {}

    /**
     * @brief Add a processor to the end of the execution chain.
     */
    void add_node(Processor* node) {
        if (node) {
            nodes_.push_back(node);
        }
    }

    /**
     * @brief Clear all nodes from the graph.
     */
    void clear() {
        nodes_.clear();
    }

    void reset() override {
        for (auto* node : nodes_) {
            node->reset();
        }
    }

    /**
     * @brief Borrow a stereo block from the graph's pool.
     */
    BufferPool::BufferPtr borrow_buffer() {
        return buffer_pool_->borrow();
    }

    /**
     * @brief Register a node for feedback (storing its last output).
     */
    void register_feedback_node(Processor* node) {
        feedback_nodes_.push_back(node);
    }

    /**
     * @brief Process an existing buffer through all nodes in the graph (Mono).
     */
    void pull_serial(std::span<float> buffer, const VoiceContext* context = nullptr) {
        for (auto* node : nodes_) {
            node->pull(buffer, context);
        }
    }

    /**
     * @brief Process an existing buffer through all nodes in the graph (Stereo).
     */
    void pull_serial(AudioBuffer& buffer, const VoiceContext* context = nullptr) {
        for (auto* node : nodes_) {
            node->pull(buffer, context);
        }
    }

protected:
    /**
     * @brief Pull through the graph (Mono).
     */
    /**
     * @brief Pull through the graph (Mono).
     */
    void do_pull(std::span<float> output, const VoiceContext* context = nullptr) override {
        if (nodes_.empty()) {
            std::fill(output.begin(), output.end(), 0.0f);
            return;
        }

        // The first node (source) fills the output span
        nodes_[0]->pull(output, context);

        // Subsequent nodes process the output span in-place
        for (size_t i = 1; i < nodes_.size(); ++i) {
            nodes_[i]->pull(output, context);
        }

        // Handle feedback nodes (store their current state as 'last output')
        // In a more complex graph, this would be part of the node execution.
    }

    /**
     * @brief Pull through the graph (Stereo).
     */
    void do_pull(AudioBuffer& output, const VoiceContext* context = nullptr) override {
        if (nodes_.empty()) {
            output.clear();
            return;
        }

        // The first node (source) fills the output buffer
        nodes_[0]->pull(output, context);

        // Subsequent nodes process the output buffer in-place
        for (size_t i = 1; i < nodes_.size(); ++i) {
            nodes_[i]->pull(output, context);
        }
    }

private:
    std::vector<Processor*> nodes_;
    std::vector<Processor*> feedback_nodes_;
    std::shared_ptr<BufferPool> buffer_pool_;
};

} // namespace audio

#endif // AUDIO_AUDIO_GRAPH_HPP
