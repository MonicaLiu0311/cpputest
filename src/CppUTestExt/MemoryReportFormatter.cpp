/*
 * Copyright (c) 2007, Michael Feathers, James Grenning and Bas Vodde
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE EARLIER MENTIONED AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTest/TestOutput.h"
#include "CppUTestExt/MemoryReportAllocator.h"
#include "CppUTest/PlatformSpecificFunctions.h"
#include "CppUTestExt/MemoryReportFormatter.h"

NormalMemoryReportFormatter::NormalMemoryReportFormatter()
{
}

NormalMemoryReportFormatter::~NormalMemoryReportFormatter()
{
}

void NormalMemoryReportFormatter::report_test_start(TestResult* result, Utest& test)
{
	result->print(StringFromFormat("TEST(%s, %s)\n", test.getGroup().asCharString(), test.getName().asCharString()).asCharString());
}

void NormalMemoryReportFormatter::report_test_end(TestResult* result, Utest& test)
{
	result->print(StringFromFormat("ENDTEST(%s, %s)\n", test.getGroup().asCharString(), test.getName().asCharString()).asCharString());
}

void NormalMemoryReportFormatter::report_alloc_memory(TestResult* result, MemoryLeakAllocator* allocator, size_t size, char* memory, const char* file, int line)
{
	result->print(StringFromFormat("Allocation using %s of size: %d pointer: %p at %s:%d\n", allocator->alloc_name(), size, memory, file, line).asCharString());
}

void NormalMemoryReportFormatter::report_free_memory(TestResult* result, MemoryLeakAllocator* allocator, char* memory, const char* file, int line)
{
	result->print(StringFromFormat("Deallocation using %s of pointer: %p at %s:%d\n", allocator->free_name(),  memory, file, line).asCharString());
}

#define MAX_VARIABLE_NAME_LINE_PART 10
#define MAX_VARIABLE_NAME_FILE_PART 53
#define MAX_VARIABLE_NAME_SEPERATOR_PART 1
#define MAX_VARIABLE_NAME_LENGTH MAX_VARIABLE_NAME_FILE_PART + MAX_VARIABLE_NAME_SEPERATOR_PART + MAX_VARIABLE_NAME_LINE_PART

struct CodeReportingAllocationNode
{
	char variableName_[MAX_VARIABLE_NAME_LENGTH + 1];
	void* memory_;
	CodeReportingAllocationNode* next_;
};

CodeMemoryReportFormatter::CodeMemoryReportFormatter(MemoryLeakAllocator* internalAllocator)
	: codeReportingList_(NULL), internalAllocator_(internalAllocator)
{
}

CodeMemoryReportFormatter::~CodeMemoryReportFormatter()
{
	while (codeReportingList_) {
		CodeReportingAllocationNode* oldNode = codeReportingList_;
		codeReportingList_ = codeReportingList_->next_;
		internalAllocator_->free_memory((char*) oldNode, __FILE__, __LINE__);
	}
}


void CodeMemoryReportFormatter::addNodeToList(const char* variableName, void* memory, CodeReportingAllocationNode* next)
{
	CodeReportingAllocationNode* newNode = (CodeReportingAllocationNode*) internalAllocator_->alloc_memory(sizeof(CodeReportingAllocationNode), __FILE__, __LINE__);
	newNode->memory_ = memory;
	newNode->next_ = next;
	PlatformSpecificStrNCpy(newNode->variableName_, variableName, MAX_VARIABLE_NAME_LENGTH);
	codeReportingList_ = newNode;
}

CodeReportingAllocationNode* CodeMemoryReportFormatter::findNode(void* memory)
{

	CodeReportingAllocationNode* current = codeReportingList_;
	while (current && current->memory_ != memory) {
		current = current->next_;
	}
	return current;
}

static SimpleString extractFileNameFromPath(const char* file)
{
	const char* fileNameOnly = file + PlatformSpecificStrLen(file);
	while (fileNameOnly != file && *fileNameOnly != '/')
		fileNameOnly--;
	if (*fileNameOnly == '/') fileNameOnly++;
	return fileNameOnly;
}

SimpleString CodeMemoryReportFormatter::createVariableNameFromFileLineInfo(const char *file, int line)
{
    SimpleString fileNameOnly = extractFileNameFromPath(file);
    fileNameOnly.replace(".", "_");
    SimpleString variableName = StringFromFormat("%s_%d", fileNameOnly.asCharString(), line);
    return variableName;
}

bool CodeMemoryReportFormatter::isNewAllocator(MemoryLeakAllocator* allocator)
{
    return PlatformSpecificStrCmp(allocator->alloc_name(), StandardNewAllocator::defaultAllocator()->alloc_name()) == 0 || PlatformSpecificStrCmp(allocator->alloc_name(), StandardNewArrayAllocator::defaultAllocator()->alloc_name()) == 0;
}

bool CodeMemoryReportFormatter::variableExists(const SimpleString& variableName)
{
	CodeReportingAllocationNode* current = codeReportingList_;
	while (current) {
		if (variableName == current->variableName_)
			return true;
		current = current->next_;
	}
	return false;
}

SimpleString CodeMemoryReportFormatter::getAllocationString(MemoryLeakAllocator* allocator, const SimpleString& variableName, size_t size)
{
	if (isNewAllocator(allocator))
		return StringFromFormat("%s%s = new char[%d]; /* using %s */", variableExists(variableName) ? "" : "char* ", variableName.asCharString(), size, allocator->alloc_name());
	else
		return StringFromFormat("%s%s = malloc(%d);", variableExists(variableName) ? "" : "void* ", variableName.asCharString(), size);
}

SimpleString CodeMemoryReportFormatter::getDeallocationString(MemoryLeakAllocator* allocator, const SimpleString& variableName, const char* file, int line)
{
	if (isNewAllocator(allocator))
		return StringFromFormat("delete [] %s; /* using %s at %s:%d */", variableName.asCharString(), allocator->free_name(), file, line);
	else
		return StringFromFormat("free(%s); /* at %s:%d */", variableName.asCharString(), file, line);
}

void CodeMemoryReportFormatter::report_test_start(TestResult* result, Utest& test)
{
	result->print(StringFromFormat("*/\nTEST(%s_memoryReport, %s)\n{ /* at %s:%d */\n",
			test.getGroup().asCharString(), test.getName().asCharString(), test.getFile().asCharString(), test.getLineNumber()).asCharString());
}

void CodeMemoryReportFormatter::report_test_end(TestResult* result, Utest&)
{
	result->print("}/*");
}

void CodeMemoryReportFormatter::report_alloc_memory(TestResult* result, MemoryLeakAllocator* allocator, size_t size, char* memory, const char* file, int line)
{
	SimpleString variableName = createVariableNameFromFileLineInfo(file, line);
	result->print(StringFromFormat("\t%s\n", getAllocationString(allocator, variableName, size).asCharString()).asCharString());
	addNodeToList(variableName.asCharString(), memory, codeReportingList_);
}

void CodeMemoryReportFormatter::report_free_memory(TestResult* result, MemoryLeakAllocator* allocator, char* memory, const char* file, int line)
{
	CodeReportingAllocationNode* node = findNode(memory);
	SimpleString variableName = node->variableName_;
	result->print(StringFromFormat("\t%s\n", getDeallocationString(allocator, variableName, file, line).asCharString()).asCharString());
}