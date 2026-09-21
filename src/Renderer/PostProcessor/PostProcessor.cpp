#include "Renderer/PostProcessor/PostProcessor.h"

using namespace SUN;

void PostProcessor::Begin(RenderImage& source, uint32_t frameIndex){
    mCurrentInput = &source;
    mFrameIndex = frameIndex;
}

void PostProcessor::Execute(PostProcessPass& pass)  {
    assert(mCurrentInput);

    //RenderImage& output = pass.GetOutput(mFrameIndex);

    PostProcessContext context = {
        .frameIndex = mFrameIndex
    };

    pass.Execute(context);
    //^mCurrentInput = &output;
}

RenderImage& PostProcessor::GetCurrentImage(uint32_t frameIndex)
{
    assert(mCurrentInput != nullptr);
    return *mCurrentInput;
}