#ifndef ARRANGETASK_IMPL_HPP
#define ARRANGETASK_IMPL_HPP

#include <random>

#include "ArrangeTask.hpp"

namespace Slic3r { namespace arr2 {

// Prepare the selected and unselected items separately. If nothing is
// selected, behaves as if everything would be selected.
template<class ArrItem>
void extract_selected(ArrangeTask<ArrItem> &task,
                      const ArrangeableModel &mdl,
                      const ArrangeableToItemConverter<ArrItem> &itm_conv)
{
    // Go through the objects and check if inside the selection
    mdl.for_each_arrangeable(
        [&task, &itm_conv](const Arrangeable &arrbl) {
            bool selected = arrbl.is_selected();
            bool printable = arrbl.is_printable();

            auto itm = itm_conv.convert(arrbl, selected ? 0 : -SCALED_EPSILON);

            auto &container_parent = printable ? task.printable :
                                                 task.unprintable;

            auto &container = selected ?
                                       container_parent.selected :
                                       container_parent.unselected;

            container.emplace_back(std::move(itm));
        });

    // If the selection was empty arrange everything
    if (task.printable.selected.empty() && task.unprintable.selected.empty()) {
        task.printable.selected.swap(task.printable.unselected);
        task.unprintable.selected.swap(task.unprintable.unselected);
    }
}

template<class ArrItem>
std::unique_ptr<ArrangeTask<ArrItem>> ArrangeTask<ArrItem>::create(
    const Scene &sc, const ArrangeableToItemConverter<ArrItem> &converter)
{
    auto task = std::make_unique<ArrangeTask<ArrItem>>();

    task->settings.set_from(sc.settings());

    task->bed = sc.bed();

    extract_selected(*task, sc.model(), converter);

    return task;
}

// Remove all items on the physical bed (not occupyable for unprintable items)
// and shift all items to the next lower bed index, so that arrange will think
// that logical bed no. 1 is the physical one
template<class ItemCont>
void prepare_fixed_unselected(ItemCont &items, int shift)
{
    for (auto &itm : items)
        set_bed_index(itm, get_bed_index(itm) - shift);

    items.erase(std::remove_if(items.begin(), items.end(),
                               [](auto &itm) { return !is_arranged(itm); }),
                items.end());
}

template<class ArrItem>
std::unique_ptr<ArrangeTaskResult>
ArrangeTask<ArrItem>::process_native(Ctl &ctl)
{
    auto result = std::make_unique<ArrangeTaskResult>();

    auto arranger = Arranger<ArrItem>::create(settings);

    class TwoStepArrangeCtl: public Ctl
    {
        Ctl &parent;
        ArrangeTask &self;
    public:
        TwoStepArrangeCtl(Ctl &p, ArrangeTask &slf) : parent{p}, self{slf} {}

        void update_status(int remaining) override
        {
            parent.update_status(remaining + self.unprintable.selected.size());
        }

        bool was_canceled() const override { return parent.was_canceled(); }

    } subctl{ctl, *this};

    auto fixed_items = printable.unselected;

    // static (unselected) unprintable objects should not be overlapped by
    // movable and printable objects
    std::copy(unprintable.unselected.begin(),
              unprintable.unselected.end(),
              std::back_inserter(fixed_items));

    arranger->arrange(printable.selected, fixed_items, bed, subctl);

    // Unprintable items should go to the first bed not containing any printable
    // items
    auto beds = std::max(get_bed_count(crange(printable.selected)),
                         get_bed_count(crange(printable.unselected)));

    // If there are no printables, leave the physical bed empty
    beds = std::max(beds, size_t{1});

    prepare_fixed_unselected(unprintable.unselected, beds);

    arranger->arrange(unprintable.selected, unprintable.unselected, bed, ctl);

    result->add_items(crange(printable.selected));

    for (auto &itm : unprintable.selected) {
        if (is_arranged(itm)) {
            int bedidx = get_bed_index(itm) + beds;
            arr2::set_bed_index(itm, bedidx);
        }

        result->add_item(itm);
    }

    return result;
}

} // namespace arr2
} // namespace Slic3r

#endif //ARRANGETASK_IMPL_HPP
