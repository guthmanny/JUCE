/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-10 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

#ifndef __JUCER_PROJECTSAVER_JUCEHEADER__
#define __JUCER_PROJECTSAVER_JUCEHEADER__


//==============================================================================
class ProjectSaver
{
public:
    ProjectSaver (Project& project_, const File& projectFile_)
        : project (project_), projectFile (projectFile_), resourceFile (project_)
    {
    }

    String save()
    {
        const File oldFile (project.getFile());
        project.setFile (projectFile);

        const String linkageMode (project.getJuceLinkageMode());

        if (linkageMode == Project::notLinkedToJuce)
        {
            hasAppHeaderFile = ! project.getProjectType().isLibrary();
            hasAppConfigFile = false;
            numJuceSourceFiles = 0;
        }
        else if (linkageMode == Project::useAmalgamatedJuce
                 || linkageMode == Project::useAmalgamatedJuceViaSingleTemplate)
        {
            hasAppHeaderFile = true;
            hasAppConfigFile = true;
            numJuceSourceFiles = 1;
        }
        else if (linkageMode == Project::useAmalgamatedJuceViaMultipleTemplates)
        {
            hasAppHeaderFile = true;
            hasAppConfigFile = true;
            numJuceSourceFiles = project.getNumSeparateAmalgamatedFiles();
        }
        else if (linkageMode == Project::useLinkedJuce)
        {
            hasAppHeaderFile = true;
            hasAppConfigFile = true;
            numJuceSourceFiles = 0;
        }
        else
        {
            jassertfalse;
        }

        hasResources = (resourceFile.getNumFiles() > 0);

        writeMainProjectFile();

        if (errors.size() == 0)
            writeJuceSourceWrappers();

        if (errors.size() == 0)
            writeProjects();

        if (errors.size() > 0)
            project.setFile (oldFile);

        return errors[0];
    }

private:
    Project& project;
    const File& projectFile;
    ResourceFile resourceFile;
    StringArray errors;

    File appConfigFile, juceHeaderFile, binaryDataCpp, pluginCharacteristicsFile;
    bool hasAppHeaderFile, hasAppConfigFile, hasResources;
    int numJuceSourceFiles;

    void writeMainProjectFile()
    {
        ScopedPointer <XmlElement> xml (project.getProjectRoot().createXml());
        jassert (xml != nullptr);

        if (xml != nullptr)
        {
            #if JUCE_DEBUG
            {
                MemoryOutputStream mo;
                project.getProjectRoot().writeToStream (mo);

                MemoryInputStream mi (mo.getData(), mo.getDataSize(), false);
                ValueTree v = ValueTree::readFromStream (mi);
                ScopedPointer <XmlElement> xml2 (v.createXml());

                // This bit just tests that ValueTree save/load works reliably.. Let me know if this asserts for you!
                jassert (xml->isEquivalentTo (xml2, true));
            }
            #endif

            MemoryOutputStream mo;
            xml->writeToStream (mo, String::empty);

            if (! FileHelpers::overwriteFileWithNewDataIfDifferent (projectFile, mo))
                errors.add ("Couldn't write to the target file!");
        }
    }

    void writeJucerComment (OutputStream& out)
    {
        out << "/*" << newLine << newLine
            << "    IMPORTANT! This file is auto-generated by the Jucer each time you save your" << newLine
            << "    project - if you alter its contents, your changes may be overwritten!" << newLine
            << newLine;
    }

    void writeAppConfig (OutputStream& out)
    {
        writeJucerComment (out);
        out << "    If you want to change any of these values, use the Jucer to do so, rather than" << newLine
            << "    editing this file directly!" << newLine
            << newLine
            << "    Any commented-out settings will fall back to using the default values that" << newLine
            << "    they are given in juce_Config.h" << newLine
            << newLine
            << "*/" << newLine << newLine;

        bool notActive = project.getJuceLinkageMode() == Project::useLinkedJuce
                            || project.getJuceLinkageMode() == Project::notLinkedToJuce;
        if (notActive)
            out << "/* NOTE: These configs aren't available when you're linking to the juce library statically!" << newLine
                << "         If you need to set a configuration that differs from the default, you'll need" << newLine
                << "         to include the amalgamated Juce files." << newLine << newLine;

        OwnedArray <Project::JuceConfigFlag> flags;
        project.getJuceConfigFlags (flags);

        for (int i = 0; i < flags.size(); ++i)
        {
            const Project::JuceConfigFlag* const f = flags[i];
            const String value (f->value.toString());

            if (value != Project::configFlagEnabled && value != Project::configFlagDisabled)
                out << "//#define  ";
            else
                out << "#define    ";

            out << f->symbol;

            if (value == Project::configFlagEnabled)
                out << " 1";
            else if (value == Project::configFlagDisabled)
                out << " 0";

            out << newLine;
        }

        if (notActive)
            out << newLine << "*/" << newLine;
    }

    void writeSourceWrapper (OutputStream& out, int fileNumber)
    {
        writeJucerComment (out);
        out << "    This file pulls in all the Juce source code, and builds it using the settings" << newLine
            << "    defined in " << appConfigFile.getFileName() << "." << newLine
            << newLine
            << "    If you want to change the method by which Juce is linked into your app, use the" << newLine
            << "    Jucer to change it, rather than trying to edit this file directly." << newLine
            << newLine
            << "*/"
            << newLine << newLine
            << CodeHelpers::createIncludeStatement (appConfigFile, appConfigFile) << newLine;

        if (fileNumber == 0)
            writeInclude (out, project.isUsingFullyAmalgamatedFile() ? "juce_amalgamated.cpp"
                                                                     : "amalgamation/juce_amalgamated_template.cpp");
        else
            writeInclude (out, "amalgamation/juce_amalgamated" + String (fileNumber) + ".cpp");
    }

    void writeAppHeader (OutputStream& out)
    {
        writeJucerComment (out);
        out << "    This is the header file that your files should include in order to get all the" << newLine
            << "    Juce library headers. You should NOT include juce.h or juce_amalgamated.h directly in" << newLine
            << "    your own source files, because that wouldn't pick up the correct Juce configuration" << newLine
            << "    options for your app." << newLine
            << newLine
            << "*/" << newLine << newLine;

        String headerGuard ("__APPHEADERFILE_" + String::toHexString (juceHeaderFile.hashCode()).toUpperCase() + "__");
        out << "#ifndef " << headerGuard << newLine
            << "#define " << headerGuard << newLine << newLine;

        if (hasAppConfigFile)
            out << CodeHelpers::createIncludeStatement (appConfigFile, appConfigFile) << newLine;

        if (project.getJuceLinkageMode() != Project::notLinkedToJuce)
        {
            writeInclude (out, (project.isUsingSingleTemplateFile() || project.isUsingMultipleTemplateFiles())
                                   ? "juce_amalgamated.h"  // could use "amalgamation/juce_amalgamated_template.h", but it's slower..
                                   : (project.isUsingFullyAmalgamatedFile()
                                        ? "juce_amalgamated.h"
                                        : "juce.h"));
        }

        if (binaryDataCpp.exists())
            out << CodeHelpers::createIncludeStatement (binaryDataCpp.withFileExtension (".h"), appConfigFile) << newLine;

        out << newLine
            << "namespace ProjectInfo" << newLine
            << "{" << newLine
            << "    const char* const  projectName    = " << CodeHelpers::addEscapeChars (project.getProjectName().toString()).quoted() << ";" << newLine
            << "    const char* const  versionString  = " << CodeHelpers::addEscapeChars (project.getVersion().toString()).quoted() << ";" << newLine
            << "    const int          versionNumber  = " << createVersionCode (project.getVersion().toString()) << ";" << newLine
            << "}" << newLine
            << newLine
            << "#endif   // " << headerGuard << newLine;
    }

    void writeInclude (OutputStream& out, const String& pathFromJuceFolder)
    {
        StringArray paths, guards;

        for (int i = project.getNumExporters(); --i >= 0;)
        {
            ScopedPointer <ProjectExporter> exporter (project.createExporter (i));

            if (exporter != nullptr)
            {
                paths.add (exporter->getIncludePathForFileInJuceFolder (pathFromJuceFolder, juceHeaderFile));
                guards.add ("defined (" + exporter->getExporterIdentifierMacro() + ")");
            }
        }

        StringArray uniquePaths (paths);
        uniquePaths.removeDuplicates (false);

        if (uniquePaths.size() == 1)
        {
            out << "#include " << paths[0] << newLine;
        }
        else
        {
            int i = paths.size();
            for (; --i >= 0;)
            {
                for (int j = i; --j >= 0;)
                {
                    if (paths[i] == paths[j] && guards[i] == guards[j])
                    {
                        paths.remove (i);
                        guards.remove (i);
                    }
                }
            }

            for (i = 0; i < paths.size(); ++i)
            {
                out << (i == 0 ? "#if " : "#elif ") << guards[i] << newLine
                    << " #include " << paths[i] << newLine;
            }

            out << "#endif" << newLine;
        }
    }

    static int countMaxPluginChannels (const String& configString, bool isInput)
    {
        StringArray configs;
        configs.addTokens (configString, ", {}", String::empty);
        configs.trim();
        configs.removeEmptyStrings();
        jassert ((configs.size() & 1) == 0);  // looks like a syntax error in the configs?

        int maxVal = 0;
        for (int i = (isInput ? 0 : 1); i < configs.size(); i += 2)
            maxVal = jmax (maxVal, configs[i].getIntValue());

        return maxVal;
    }

    static String createVersionCode (const String& version)
    {
        StringArray configs;
        configs.addTokens (version, ",.", String::empty);
        configs.trim();
        configs.removeEmptyStrings();

        int value = (configs[0].getIntValue() << 16) + (configs[1].getIntValue() << 8) + configs[2].getIntValue();

        if (configs.size() >= 4)
            value = (value << 8) + configs[3].getIntValue();

        return "0x" + String::toHexString (value);
    }

    void writePluginCharacteristics (OutputStream& out)
    {
        String headerGuard ("__PLUGINCHARACTERISTICS_" + String::toHexString (pluginCharacteristicsFile.hashCode()).toUpperCase() + "__");

        writeJucerComment (out);
        out << "    This header file contains configuration options for the plug-in. If you need to change any of" << newLine
            << "    these, it'd be wise to do so using the Jucer, rather than editing this file directly..." << newLine
            << newLine
            << "*/" << newLine
            << newLine
            << "#ifndef " << headerGuard << newLine
            << "#define " << headerGuard << newLine
            << newLine
            << "#define JucePlugin_Build_VST    " << ((bool) project.shouldBuildVST().getValue() ? 1 : 0) << "  // (If you change this value, you'll also need to re-export the projects using the Jucer)" << newLine
            << "#define JucePlugin_Build_AU     " << ((bool) project.shouldBuildAU().getValue() ? 1 : 0) << "  // (If you change this value, you'll also need to re-export the projects using the Jucer)" << newLine
            << "#define JucePlugin_Build_RTAS   " << ((bool) project.shouldBuildRTAS().getValue() ? 1 : 0) << "  // (If you change this value, you'll also need to re-export the projects using the Jucer)" << newLine
            << newLine
            << "#define JucePlugin_Name                 " << project.getPluginName().toString().quoted() << newLine
            << "#define JucePlugin_Desc                 " << project.getPluginDesc().toString().quoted() << newLine
            << "#define JucePlugin_Manufacturer         " << project.getPluginManufacturer().toString().quoted() << newLine
            << "#define JucePlugin_ManufacturerCode     '" << project.getPluginManufacturerCode().toString().trim().substring (0, 4) << "'" << newLine
            << "#define JucePlugin_PluginCode           '" << project.getPluginCode().toString().trim().substring (0, 4) << "'" << newLine
            << "#define JucePlugin_MaxNumInputChannels  " << countMaxPluginChannels (project.getPluginChannelConfigs().toString(), true) << newLine
            << "#define JucePlugin_MaxNumOutputChannels " << countMaxPluginChannels (project.getPluginChannelConfigs().toString(), false) << newLine
            << "#define JucePlugin_PreferredChannelConfigurations   " << project.getPluginChannelConfigs().toString() << newLine
            << "#define JucePlugin_IsSynth              " << ((bool) project.getPluginIsSynth().getValue() ? 1 : 0) << newLine
            << "#define JucePlugin_WantsMidiInput       " << ((bool) project.getPluginWantsMidiInput().getValue() ? 1 : 0) << newLine
            << "#define JucePlugin_ProducesMidiOutput   " << ((bool) project.getPluginProducesMidiOut().getValue() ? 1 : 0) << newLine
            << "#define JucePlugin_SilenceInProducesSilenceOut  " << ((bool) project.getPluginSilenceInProducesSilenceOut().getValue() ? 1 : 0) << newLine
            << "#define JucePlugin_TailLengthSeconds    " << (double) project.getPluginTailLengthSeconds().getValue() << newLine
            << "#define JucePlugin_EditorRequiresKeyboardFocus  " << ((bool) project.getPluginEditorNeedsKeyFocus().getValue() ? 1 : 0) << newLine
            << "#define JucePlugin_VersionCode          " << createVersionCode (project.getVersion().toString()) << newLine
            << "#define JucePlugin_VersionString        " << project.getVersion().toString().quoted() << newLine
            << "#define JucePlugin_VSTUniqueID          JucePlugin_PluginCode" << newLine
            << "#define JucePlugin_VSTCategory          " << ((bool) project.getPluginIsSynth().getValue() ? "kPlugCategSynth" : "kPlugCategEffect") << newLine
            << "#define JucePlugin_AUMainType           " << ((bool) project.getPluginIsSynth().getValue() ? "kAudioUnitType_MusicDevice" : "kAudioUnitType_Effect") << newLine
            << "#define JucePlugin_AUSubType            JucePlugin_PluginCode" << newLine
            << "#define JucePlugin_AUExportPrefix       " << project.getPluginAUExportPrefix().toString() << newLine
            << "#define JucePlugin_AUExportPrefixQuoted " << project.getPluginAUExportPrefix().toString().quoted() << newLine
            << "#define JucePlugin_AUManufacturerCode   JucePlugin_ManufacturerCode" << newLine
            << "#define JucePlugin_CFBundleIdentifier   " << project.getBundleIdentifier().toString() << newLine
            << "#define JucePlugin_AUCocoaViewClassName " << project.getPluginAUCocoaViewClassName().toString() << newLine
            << "#define JucePlugin_RTASCategory         " << ((bool) project.getPluginIsSynth().getValue() ? "ePlugInCategory_SWGenerators" : "ePlugInCategory_None") << newLine
            << "#define JucePlugin_RTASManufacturerCode JucePlugin_ManufacturerCode" << newLine
            << "#define JucePlugin_RTASProductId        JucePlugin_PluginCode" << newLine;

        out << "#define JUCE_USE_VSTSDK_2_4             1" << newLine
            << newLine
            << "#endif   // " << headerGuard << newLine;
    }

    bool replaceFileIfDifferent (const File& f, const MemoryOutputStream& newData)
    {
        if (! FileHelpers::overwriteFileWithNewDataIfDifferent (f, newData))
        {
            errors.add ("Can't write to file: " + f.getFullPathName());
            return false;
        }

        return true;
    }

    void writeJuceSourceWrappers()
    {
        const File wrapperFolder (project.getWrapperFolder());

        appConfigFile = wrapperFolder.getChildFile (project.getAppConfigFilename());
        pluginCharacteristicsFile = wrapperFolder.getChildFile (project.getPluginCharacteristicsFilename());

        juceHeaderFile = project.getAppIncludeFile();
        binaryDataCpp = wrapperFolder.getChildFile ("BinaryData.cpp");

        if (resourceFile.getNumFiles() > 0)
        {
            if (! wrapperFolder.createDirectory())
            {
                errors.add ("Couldn't create folder: " + wrapperFolder.getFullPathName());
                return;
            }

            //resourceFile.setJuceHeaderToInclude (juceHeaderFile);
            resourceFile.setClassName ("BinaryData");

            if (! resourceFile.write (binaryDataCpp))
                errors.add ("Can't create binary resources file: " + binaryDataCpp.getFullPathName());
        }
        else
        {
            binaryDataCpp.deleteFile();
            binaryDataCpp.withFileExtension ("h").deleteFile();
        }

        if (project.getProjectType().isLibrary())
            return;

        if (! wrapperFolder.createDirectory())
        {
            errors.add ("Couldn't create folder: " + wrapperFolder.getFullPathName());
            return;
        }

        if (hasAppConfigFile)
        {
            MemoryOutputStream mem;
            writeAppConfig (mem);
            replaceFileIfDifferent (appConfigFile, mem);
        }
        else
        {
            appConfigFile.deleteFile();
        }

        if (project.getProjectType().isAudioPlugin())
        {
            MemoryOutputStream mem;
            writePluginCharacteristics (mem);
            replaceFileIfDifferent (pluginCharacteristicsFile, mem);
        }

        for (int i = 0; i <= project.getNumSeparateAmalgamatedFiles(); ++i)
        {
            const File sourceWrapperCpp (getSourceWrapperCpp (i));
            const File sourceWrapperMM (sourceWrapperCpp.withFileExtension (".mm"));

            if (numJuceSourceFiles > 0
                 && ((i == 0 && numJuceSourceFiles == 1) || (i != 0 && numJuceSourceFiles > 1)))
            {
                MemoryOutputStream mem;
                writeSourceWrapper (mem, i);
                replaceFileIfDifferent (sourceWrapperCpp, mem);
                replaceFileIfDifferent (sourceWrapperMM, mem);
            }
            else
            {
                sourceWrapperMM.deleteFile();
                sourceWrapperCpp.deleteFile();
            }
        }

        if (hasAppHeaderFile)
        {
            MemoryOutputStream mem;
            writeAppHeader (mem);
            replaceFileIfDifferent (juceHeaderFile, mem);
        }
        else
        {
            juceHeaderFile.deleteFile();
        }
    }

    void writeProjects()
    {
        for (int i = project.getNumExporters(); --i >= 0;)
        {
            ScopedPointer <ProjectExporter> exporter (project.createExporter (i));
            std::cout << "Writing files for: " << exporter->getName() << std::endl;

            const File targetFolder (exporter->getTargetFolder());

            if (targetFolder.createDirectory())
            {
                exporter->juceWrapperFolder = RelativePath (project.getWrapperFolder(), targetFolder, RelativePath::buildTargetFolder);

                if (hasAppConfigFile)
                    exporter->juceWrapperFiles.add (RelativePath (appConfigFile, targetFolder, RelativePath::buildTargetFolder));

                if (hasAppHeaderFile)
                    exporter->juceWrapperFiles.add (RelativePath (juceHeaderFile, targetFolder, RelativePath::buildTargetFolder));

                if (hasResources)
                {
                    exporter->juceWrapperFiles.add (RelativePath (binaryDataCpp, targetFolder, RelativePath::buildTargetFolder));
                    exporter->juceWrapperFiles.add (RelativePath (binaryDataCpp, targetFolder, RelativePath::buildTargetFolder)
                                                        .withFileExtension (".h"));
                }

                if (numJuceSourceFiles > 0)
                {
                    for (int j = 0; j <= project.getNumSeparateAmalgamatedFiles(); ++j)
                    {
                        const File sourceWrapperCpp (getSourceWrapperCpp (j));
                        const File sourceWrapperMM (sourceWrapperCpp.withFileExtension (".mm"));

                        if ((j == 0 && numJuceSourceFiles == 1) || (j != 0 && numJuceSourceFiles > 1))
                        {
                            if (exporter->usesMMFiles())
                                exporter->juceWrapperFiles.add (RelativePath (sourceWrapperMM, targetFolder, RelativePath::buildTargetFolder));
                            else
                                exporter->juceWrapperFiles.add (RelativePath (sourceWrapperCpp, targetFolder, RelativePath::buildTargetFolder));
                        }
                    }
                }

                if (project.getProjectType().isAudioPlugin())
                    exporter->juceWrapperFiles.add (RelativePath (pluginCharacteristicsFile, targetFolder, RelativePath::buildTargetFolder));

                try
                {
                    exporter->create();
                }
                catch (ProjectExporter::SaveError& error)
                {
                    errors.add (error.message);
                }
            }
            else
            {
                errors.add ("Can't create folder: " + exporter->getTargetFolder().getFullPathName());
            }
        }
    }

    File getSourceWrapperCpp (int fileIndex) const
    {
        return project.getWrapperFolder().getChildFile (project.getJuceSourceFilenameRoot() + (fileIndex != 0 ? String (fileIndex) : String::empty))
                                         .withFileExtension (".cpp");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectSaver);
};


#endif
